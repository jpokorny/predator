/*
 * Copyright (C) 2010 Jiri Simacek
 *
 * This file is part of predator.
 *
 * predator is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * any later version.
 *
 * predator is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with predator.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <sstream>
#include <vector>
#include <list>
#include <set>
#include <boost/unordered_set.hpp>
#include <boost/unordered_map.hpp>

#include <cl/code_listener.h>
#include <cl/cl_msg.hh>
#include <cl/cldebug.hh>
#include <cl/clutil.hh>
#include <cl/storage.hh>

#include "treeaut.hh"
#include "forestaut.hh"
#include "forestautext.hh"
#include "ufae.hh"
#include "nodebuilder.hh"
#include "operandinfo.hh"
#include "symctx.hh"
#include "symstate.hh"
#include "loopanalyser.hh"
#include "builtintable.hh"

#include "symexec.hh"

using std::vector;
using std::list;
using std::set;
using boost::unordered_set;
using boost::unordered_map;

void dumpOperandTypes(std::ostream& os, const cl_operand* op) {
	os << "operand:" << std::endl;
	cltToStream(os, op->type, false);
	os << "accessors:" << std::endl;
	const cl_accessor* acc = op->accessor;
	while (acc) {
		cltToStream(os, acc->type, false);
		acc = acc->next;
	}
}

std::ostream& operator<<(std::ostream& os, const cl_location& loc) {
	if (loc.file)
		return os << loc.file << ':' << loc.line << ':';
	else
		return os << "<unknown location>:";
}
/*
struct SymOp {

	SymState* src;
	SymState* dst;

};
*/
#define STATE_FROM_FAE(fae) ((SymState*)(assert((fae).varGet(IP_INDEX).isNativePtr()), (fae).varGet(IP_INDEX).d_native_ptr))

typedef enum { itDenormalize, itReverse }  tr_item_type;

struct TraceRecorder {

	struct Item {

		Item* parent;
		tr_item_type itemType;
		const FAE* fae;
		std::list<const FAE*>::iterator queueTag;
		FAE::NormInfo normInfo;
		set<Item*> children;

		Item(Item* parent, const FAE* fae, std::list<const FAE*>::iterator queueTag, const FAE::NormInfo& normInfo)
			: parent(parent), itemType(tr_item_type::itDenormalize), fae(fae), queueTag(queueTag), normInfo(normInfo) {
			if (parent)
				parent->children.insert(this);
		}

		Item(Item* parent, const FAE* fae)
			: parent(parent), itemType(tr_item_type::itReverse), fae(fae) {
			if (parent)
				parent->children.insert(this);
		}

		~Item() {
			assert(this->fae);
			delete this->fae;
			this->fae = NULL;
		}

		void removeChild(Item* item) {
			size_t s = this->children.erase(item);
			assert(s == 1);
		}

	};

	unordered_map<const FAE*, Item*> confMap;

	TraceRecorder() {
	}

	~TraceRecorder() { this->clear(); }

	void clear() {
		utils::eraseMap(this->confMap);
	}

	void init(const FAE* fae, std::list<const FAE*>::iterator i) {
		this->clear();
		Item* item = new Item(NULL, fae, i, FAE::NormInfo());
		this->confMap.insert(make_pair(fae, item));
	}

	Item* find(const FAE* fae) {
		unordered_map<const FAE*, Item*>::iterator i = this->confMap.find(fae);
		assert(i != this->confMap.end());
		return i->second;
	}

	void add(const FAE* parent, const FAE* fae, std::list<const FAE*>::iterator i, const FAE::NormInfo& normInfo) {
		this->confMap.insert(
			make_pair(fae, new Item(this->find(parent), fae, i, normInfo))
		);
	}

	void add(const FAE* parent, const FAE* fae) {
		this->confMap.insert(
			make_pair(fae, new Item(this->find(parent), fae))
		);
	}

	void remove(const FAE* fae) {
		unordered_map<const FAE*, Item*>::iterator i = this->confMap.find(fae);
		assert(i != this->confMap.end());
		delete i->second;
		this->confMap.erase(i);
	}

	template <class F>
	void invalidate(TraceRecorder::Item* node, F f) {

		f(node);

		for (set<Item*>::iterator i = node->children.begin(); i != node->children.end(); ++i)
			this->invalidate(*i, f);

		const FAE* fae = node->fae;

		this->remove(fae);

	}

	template <class F>
	void invalidateChildren(TraceRecorder::Item* node, F f) {

		for (set<Item*>::iterator i = node->children.begin(); i != node->children.end(); ++i)
			this->invalidate(*i, f);

		node->children.clear();
	
	}

	template <class F>
	void destroyBranch(const FAE* fae, F f) {
		TraceRecorder::Item* node = this->find(fae);
		assert(node);
		while (node->children.empty()) {
			TraceRecorder::Item* parent = node->parent;
			if (!parent) {
				this->remove(node->fae);
				return;
			}
			parent->removeChild(node);
			f(node);
			this->remove(node->fae);
			node = parent;
		}
	}

};

class SymExec::Engine {

	const CodeStorage::Storage& stor;

	LoopAnalyser loopAnalyser;

	TA<label_type>::Backend taBackend;
	TA<label_type>::Backend fixpointBackend;
	TA<label_type>::Manager taMan;
	BoxMan boxMan;

	typedef unordered_map<const CodeStorage::Fnc*, SymCtx*> ctx_store_type;
	ctx_store_type ctxStore;

	typedef unordered_map<const CodeStorage::Insn*, SymState*> state_store_type;
	state_store_type stateStore;

	std::list<const FAE*> queue;

	const FAE* currentConf;
	const CodeStorage::Insn* currentInsn;
	
	TraceRecorder traceRecorder;

	std::vector<const Box*> boxes;
	std::vector<const Box*> basicBoxes;
	boost::unordered_map<const Box*, std::vector<const Box*> > hierarchy;

	bool dbgFlag;

	size_t statesEvaluated;
	size_t tracesEvaluated;

protected:

	SymCtx* getCtx(const CodeStorage::Fnc* fnc) {

		ctx_store_type::iterator i = this->ctxStore.find(fnc);
		if (i != this->ctxStore.end())
			return i->second;

		return this->ctxStore.insert(make_pair(fnc, new SymCtx(*fnc))).first->second;

	}

	SymState* findState(const CodeStorage::Insn* insn) {

		state_store_type::iterator i = this->stateStore.find(insn);
		assert(i != this->stateStore.end());
		return i->second;

	}

	SymState* getState(const CodeStorage::Block::const_iterator& insn, const SymCtx* ctx) {

		state_store_type::iterator i = this->stateStore.find(*insn);
		if (i != this->stateStore.end())
			return i->second;

		SymState* s = new SymState(this->taBackend, this->fixpointBackend, this->boxMan);
		s->insn = insn;
		s->ctx = ctx;
		s->entryPoint = this->loopAnalyser.isEntryPoint(*insn);
		
		return this->stateStore.insert(make_pair(*insn, s)).first->second;

	}

	struct ExactTMatchF {
		bool operator()(const TT<label_type>& t1, const TT<label_type>& t2) {
			return t1.label() == t2.label();
		}
	};

	struct SmartTMatchF {
		bool operator()(const TT<label_type>& t1, const TT<label_type>& t2) {
			if (t1.label()->isNode() && t2.label()->isNode())
				return t1.label()->getTag() == t2.label()->getTag();
			return t1.label() == t2.label();
		}
	};

	struct SmarterTMatchF {
		bool operator()(const TT<label_type>& t1, const TT<label_type>& t2) {
			if (t1.label()->isNode() && t2.label()->isNode()) {
				if (t1.label()->getTag() != t2.label()->getTag())
					return false;
				std::vector<size_t> tmp;
				for (std::vector<size_t>::const_iterator i = t1.lhs().begin(); i != t1.lhs().end(); ++i) {
					if (FA::isData(*i))
						tmp.push_back(*i);
				}
				size_t i = 0;
				for (std::vector<size_t>::const_iterator j = t2.lhs().begin(); j != t2.lhs().end(); ++j) {
					if (FA::isData(*j)) {
						if ((i >= tmp.size()) || (*j != tmp[i++]))
							return false;
					}
				}
				return (i == tmp.size());
			}
			return t1.label() == t2.label();
		}
	};

/*
	bool foldBox(SymState* target, FAE& fae, size_t root, const Box* box) {
		CL_CDEBUG("trying " << *(const AbstractBox*)box << " at " << root);
		if (!fae.foldBox(root, box))
			return false;
		CL_CDEBUG("match");
		std::set<size_t> tmp;
		fae.getNearbyReferences(fae.varGet(ABP_INDEX).d_ref.root, tmp);
		FAE::NormInfo normInfo;
		fae.normalize(normInfo, tmp);
		boost::unordered_map<const Box*, std::vector<const Box*> >::iterator i =
			this->hierarchy.find(box);
		if (i == this->hierarchy.end())
			return true;
		this->recAbstractAndFold(target, fae, i->second);
		return true;
	}

	void recAbstractAndFold(SymState* target, FAE& fae, const std::vector<const Box*>& boxes) {

		CL_CDEBUG("abstracting and folding ... " << target->absHeight);
//		fae.heightAbstraction(target->absHeight, ExactLabelMatchF());
		CL_CDEBUG(std::endl << fae);

		// do not fold at 0
		for (size_t i = 1; i < fae.getRootCount(); ++i) {
			fae.heightAbstraction(i, target->absHeight, ExactLabelMatchF());
			for (std::vector<const Box*>::const_iterator j = boxes.begin(); j != boxes.end(); ++j) {
				if (this->foldBox(target, fae, i, *j))
					i = 1;
			}
		}

	}
*/

	struct CompareVariablesF {
		bool operator()(size_t i, const TA<label_type>& ta1, const TA<label_type>& ta2) {
			if (i)
				return true;
			const TT<label_type>& t1 = ta1.getAcceptingTransition();
			const TT<label_type>& t2 = ta2.getAcceptingTransition();
			return (t1.label() == t2.label()) && (t1.lhs() == t2.lhs());
		}
	};

	struct FuseNonZeroF {
		bool operator()(size_t root, const FAE&) {
			return root != 0;
		}
	};

	void mergeFixpoint(SymState* target, FAE& fae) {
		std::vector<FAE*> tmp;
		ContainerGuard<std::vector<FAE*> > g(tmp);
		FAE::loadCompatibleFAs(tmp, target->fwdConf, this->taMan, this->boxMan, &fae, 0, CompareVariablesF());
//		for (size_t i = 0; i < tmp.size(); ++i)
//			CL_CDEBUG("accelerator " << std::endl << *tmp[i]);
		fae.fuse(tmp, FuseNonZeroF());
		CL_CDEBUG("fused " << std::endl << fae);
	}

	void fold(SymState* target, FAE& fae) {

		bool matched = false;

		// do not fold at 0
		for (size_t i = 1; i < fae.getRootCount(); ++i) {
			for (std::vector<const Box*>::const_iterator j = this->boxes.begin(); j != this->boxes.end(); ++j) {
				CL_CDEBUG("trying " << *(const AbstractBox*)*j << " at " << i);
				if (fae.foldBox(i, *j)) {
					matched = true;
					CL_CDEBUG("match");
				}
			}
		}

		if (matched) {
			std::set<size_t> tmp;
			fae.getNearbyReferences(fae.varGet(ABP_INDEX).d_ref.root, tmp);
			FAE::NormInfo normInfo;
			fae.normalize(normInfo, tmp);
		}

	}

	void abstract(SymState* target, FAE& fae) {

//		this->recAbstractAndFold(target, fae, this->basicBoxes);

		CL_CDEBUG("abstracting ... " << target->absHeight);
		for (size_t i = 1; i < fae.getRootCount(); ++i)
			fae.heightAbstraction(i, target->absHeight, SmartTMatchF());

	}

	struct DestroySimpleF {

		DestroySimpleF() {}

		void operator()(TraceRecorder::Item* node) {

			SymState* state = STATE_FROM_FAE(*node->fae);
			if (state->entryPoint)
				STATE_FROM_FAE(*node->fae)->extendFixpoint(node->fae);
//			else
//				STATE_FROM_FAE(*node->fae)->invalidate(node->fae);			

		}

	};

	void enqueue(SymState* target, const FAE* parent, FAE* fae) {

		Guard<FAE> g(fae);

		fae->varSet(IP_INDEX, Data::createNativePtr((void*)target));

		std::set<size_t> tmp;
		FAE::NormInfo normInfo;
		fae->getNearbyReferences(fae->varGet(ABP_INDEX).d_ref.root, tmp);

//		CL_CDEBUG("before normalization: " << std::endl << *fae); 

		fae->normalize(normInfo, tmp);

//		CL_CDEBUG("after normalization: " << std::endl << *fae); 

//		CL_CDEBUG("normInfo: " << std::endl << normInfo); 

//		normInfo.check();

//		FAE normalized(fae);

		if (target->entryPoint) {
			this->fold(target, *fae);
			this->mergeFixpoint(target, *fae);
			this->abstract(target, *fae);
/*			if (target->absHeight > 1)
				fae->minimizeRootsCombo();*/
		}

		g.release();

		std::list<const FAE*>::iterator k = this->queue.insert(this->queue.end(), fae);
		this->traceRecorder.add(parent, fae, k, normInfo);

		CL_CDEBUG("enqueued: " << *k << std::endl << **k);

	}

	void enqueueNextInsn(SymState* state, const FAE* parent, FAE* fae) {

		state->finalizeOperands(*fae);

		this->enqueue(this->getState(state->insn + 1, state->ctx), parent, fae);
		
	}

	void execAssignment(SymState* state, const FAE* parent, const CodeStorage::Insn* insn) {

		OperandInfo dst, src;
		state->ctx->parseOperand(dst, *parent, &insn->operands[0]);
		state->ctx->parseOperand(src, *parent, &insn->operands[1]);

		assert(src.type->code == dst.type->code);

		FAE* fae = new FAE(*parent);
		Guard<FAE> g(fae);

		RevInfo rev;

		if (
			src.type->code == cl_type_e::CL_TYPE_PTR &&
			src.type->items[0].type->code == cl_type_e::CL_TYPE_VOID &&
			dst.type->items[0].type->code != cl_type_e::CL_TYPE_VOID
		) {
			Data data = src.readData(*fae, itov((size_t)0));
			assert(data.isVoidPtr());
			if (dst.type->items[0].type->size != (int)data.d_void_ptr)
				throw ProgramError("allocated block's size mismatch");
			vector<SelData> sels;
			NodeBuilder::buildNode(sels, dst.type->items[0].type);
			std::string typeName;
			if (dst.type->items[0].type->name)
				typeName = std::string(dst.type->items[0].type->name);
			else {
				std::ostringstream ss;
				ss << dst.type->items[0].type->uid;
				typeName = ss.str();
			}
			dst.writeData(
				*fae,
				Data::createRef(fae->nodeCreate(sels, this->boxMan.getTypeInfo(typeName))),
				rev
			);
		} else {
//			assert(*(src.type) == *(dst.type));
			vector<size_t> offs;
			NodeBuilder::buildNode(offs, dst.type);
			dst.writeData(*fae, src.readData(*fae, offs), rev);
		}

		g.release();

		this->enqueueNextInsn(state, parent, fae);
		
	}

	void execTruthNot(SymState* state, const FAE* parent, const CodeStorage::Insn* insn) {

		OperandInfo dst, src;
		state->ctx->parseOperand(dst, *parent, &insn->operands[0]);
		state->ctx->parseOperand(src, *parent, &insn->operands[1]);

		assert(dst.type->code == cl_type_e::CL_TYPE_BOOL);
		assert(src.type->code == cl_type_e::CL_TYPE_BOOL || src.type->code == cl_type_e::CL_TYPE_INT);

		vector<size_t> offs;
		NodeBuilder::buildNode(offs, src.type);

		Data data = src.readData(*parent, offs), res;

		switch (data.type) {
			case data_type_e::t_bool:
				res = Data::createBool(!data.d_bool);
				break;
			case data_type_e::t_int:
				res = Data::createBool(!data.d_int);
				break;
			default:
				assert(false);
		}

		RevInfo rev;

		FAE* fae = new FAE(*parent);
		Guard<FAE> g(fae);
		dst.writeData(*fae, res, rev);

		g.release();

		this->enqueueNextInsn(state, parent, fae);
		
	}

	static void dataEq(const Data& x, const Data& y, bool neg, vector<Data>& res) {
		if ((x.isUnknw() || x.isUndef()) || (y.isUnknw() || y.isUndef())) {
			if ((float)random()/RAND_MAX < 0.5) {
				res.push_back(Data::createBool(false));
				res.push_back(Data::createBool(true));
			} else {
				res.push_back(Data::createBool(true));
				res.push_back(Data::createBool(false));
			}
		} else
			res.push_back(Data::createBool((x == y) != neg));
	}

	void execEq(SymState* state, const FAE* parent, const CodeStorage::Insn* insn, bool neg) {

		OperandInfo dst, src1, src2;
		state->ctx->parseOperand(dst, *parent, &insn->operands[0]);
		state->ctx->parseOperand(src1, *parent, &insn->operands[1]);
		state->ctx->parseOperand(src2, *parent, &insn->operands[2]);

//		assert(*src1.type == *src2.type);
		assert(OperandInfo::isLValue(dst.flag));
		assert(dst.type->code == cl_type_e::CL_TYPE_BOOL);

		vector<size_t> offs1;
		NodeBuilder::buildNode(offs1, src1.type);

		vector<size_t> offs2;
		NodeBuilder::buildNode(offs2, src2.type);

		Data data1 = src1.readData(*parent, offs1);
		Data data2 = src2.readData(*parent, offs2);
		vector<Data> res;
		Engine::dataEq(data1, data2, neg, res);
		RevInfo rev;
		for (vector<Data>::iterator j = res.begin(); j != res.end(); ++j) {
			FAE* fae = new FAE(*parent);
			Guard<FAE> g(fae);
			dst.writeData(*fae, *j, rev);
			g.release();
			this->enqueueNextInsn(state, parent, fae);
		}

	}

	void execPlus(SymState* state, const FAE* parent, const CodeStorage::Insn* insn) {

		OperandInfo dst, src1, src2;
		state->ctx->parseOperand(dst, *parent, &insn->operands[0]);
		state->ctx->parseOperand(src1, *parent, &insn->operands[1]);
		state->ctx->parseOperand(src2, *parent, &insn->operands[2]);

		assert(dst.type->code == cl_type_e::CL_TYPE_INT);
		assert(src1.type->code == cl_type_e::CL_TYPE_INT);
		assert(src2.type->code == cl_type_e::CL_TYPE_INT);

		vector<size_t> offs1, offs2;
		NodeBuilder::buildNode(offs1, src1.type);
		NodeBuilder::buildNode(offs2, src2.type);

		Data data1 = src1.readData(*parent, offs1);
		Data data2 = src2.readData(*parent, offs2);
		assert(data1.isInt() && data2.isInt());
		Data res = Data::createInt((data1.d_int + data2.d_int > 0)?(1):(0));

		RevInfo rev;

		FAE* fae = new FAE(*parent);
		Guard<FAE> g(fae);
		dst.writeData(*fae, res, rev);

		g.release();

		this->enqueueNextInsn(state, parent, fae);
		
	}

	void execPointerPlus(SymState* state, const FAE* parent, const CodeStorage::Insn* insn) {

		OperandInfo dst, src1, src2;
		state->ctx->parseOperand(dst, *parent, &insn->operands[0]);
		state->ctx->parseOperand(src1, *parent, &insn->operands[1]);
		state->ctx->parseOperand(src2, *parent, &insn->operands[2]);

		assert(dst.type->code == cl_type_e::CL_TYPE_PTR);
		assert(src1.type->code == cl_type_e::CL_TYPE_PTR);
		assert(src2.type->code == cl_type_e::CL_TYPE_INT);

		vector<size_t> offs1, offs2;
		NodeBuilder::buildNode(offs1, src1.type);
		NodeBuilder::buildNode(offs2, src2.type);

		Data data1 = src1.readData(*parent, offs1);
		Data data2 = src2.readData(*parent, offs2);
		assert(data1.isRef() && data2.isInt());
		Data res = Data::createRef(data1.d_ref.root, data1.d_ref.displ + data2.d_int);

		RevInfo rev;

		FAE* fae = new FAE(*parent);
		Guard<FAE> g(fae);
		dst.writeData(*fae, res, rev);

		g.release();

		this->enqueueNextInsn(state, parent, fae);
		
	}

	void execMalloc(SymState* state, const FAE* parent, const CodeStorage::Insn* insn) {

		OperandInfo dst, src;
		state->ctx->parseOperand(dst, *parent, &insn->operands[0]);
		state->ctx->parseOperand(src, *parent, &insn->operands[2]);
		assert(src.type->code == cl_type_e::CL_TYPE_INT);

		Data data = src.readData(*parent, itov((size_t)0));
		assert(data.isInt());
		RevInfo rev;
		
		FAE* fae = new FAE(*parent);
		Guard<FAE> g(fae);
		dst.writeData(*fae, Data::createVoidPtr(data.d_int), rev);

		g.release();
		
		this->enqueueNextInsn(state, parent, fae);
	
	}

	void execFree(SymState* state, const FAE* parent, const CodeStorage::Insn* insn) {

		OperandInfo src;
		state->ctx->parseOperand(src, *parent, &insn->operands[2]);
		Data data = src.readData(*parent, itov((size_t)0));
		if (!data.isRef())
			throw ProgramError("releasing non-pointer value");
		if (data.d_ref.displ != 0)
			throw ProgramError("releasing a pointer which points inside the block");
		FAE* fae = new FAE(*parent);
		Guard<FAE> g(fae);
		fae->nodeDelete(data.d_ref.root);
		g.release();
		this->enqueueNextInsn(state, parent, fae);
		
	}

	void execNondet(SymState* state, const FAE* parent, const CodeStorage::Insn* insn) {

		OperandInfo dst;
		state->ctx->parseOperand(dst, *parent, &insn->operands[0]);

		RevInfo rev;

		FAE* fae = new FAE(*parent);
		Guard<FAE> g(fae);

		dst.writeData(*fae, Data::createUnknw(), rev);

		g.release();
		
		this->enqueueNextInsn(state, parent, fae);

	}

	void execJmp(SymState* state, const FAE* parent, const CodeStorage::Insn* insn) {

		this->enqueue(this->getState(insn->targets[0]->begin(), state->ctx), parent, new FAE(*parent));

	}

	void execCond(SymState* state, const FAE* parent, const CodeStorage::Insn* insn) {

		OperandInfo src;
		state->ctx->parseOperand(src, *parent, &insn->operands[0]);

		assert(src.type->code == cl_type_e::CL_TYPE_BOOL);

		Data data = src.readData(*parent, itov((size_t)0));

		if (!data.isBool())
			throw runtime_error("Engine::execCond(): non boolean condition argument!");

		FAE* fae = new FAE(*parent);
		state->finalizeOperands(*fae);
		this->enqueue(this->getState(insn->targets[((data.d_bool))?(0):(1)]->begin(), state->ctx), parent, fae);

	}

	// TODO: implement proper return
	void execRet(SymState* state, const FAE* parent, const CodeStorage::Insn* insn) {

		FAE fae(*parent);

		bool b = state->ctx->destroyStackFrame(fae);
		assert(!b);

		fae.check();
		
	}

	void execInsn(SymState* state, const FAE* parent) {

		const CodeStorage::Insn* insn = *state->insn;

		switch (insn->code) {

			case cl_insn_e::CL_INSN_UNOP:
				switch (insn->subCode) {
					case cl_unop_e::CL_UNOP_ASSIGN:
						this->execAssignment(state, parent, insn);
						break;
					case cl_unop_e::CL_UNOP_TRUTH_NOT:
						this->execTruthNot(state, parent, insn);
						break;
					default:
						throw std::runtime_error("feature not implemented");
				}
				break;

			case cl_insn_e::CL_INSN_BINOP:
				switch (insn->subCode) {
					case cl_binop_e::CL_BINOP_EQ:
						this->execEq(state, parent, insn, false);
						break;
					case cl_binop_e::CL_BINOP_NE:
						this->execEq(state, parent, insn, true);
						break;
					case cl_binop_e::CL_BINOP_PLUS:
						this->execPlus(state, parent, insn);
						break;
/*					case cl_binop_e::CL_BINOP_MINUS:
						this->execMinus(state, parent, insn);
						break;*/
					case cl_binop_e::CL_BINOP_POINTER_PLUS:
						this->execPointerPlus(state, parent, insn);
						break;
					default:
						throw std::runtime_error("feature not implemented");
				}
				break;

			case cl_insn_e::CL_INSN_CALL:
				assert(insn->operands[1].code == cl_operand_e::CL_OPERAND_CST);
				assert(insn->operands[1].data.cst.code == cl_type_e::CL_TYPE_FNC);
				switch (BuiltinTableStatic::data[insn->operands[1].data.cst.data.cst_fnc.name]) {
					case builtin_e::biMalloc:
						this->execMalloc(state, parent, insn);
						break;
					case builtin_e::biFree:
						this->execFree(state, parent, insn);
						break;
					case builtin_e::biNondet:
						this->execNondet(state, parent, insn);
						break;
					default:
						throw std::runtime_error("feature not implemented");
				}
				break;

			case cl_insn_e::CL_INSN_RET:
				this->execRet(state, parent, insn);
				break;

			case cl_insn_e::CL_INSN_JMP:
				this->execJmp(state, parent, insn);
				break;

			case cl_insn_e::CL_INSN_COND:
				this->execCond(state, parent, insn);
				break;

			default:
				throw std::runtime_error("feature not implemented");

		}
				
	}

	struct InvalidateF {

		list<const FAE*>& queue;
		set<SymState*>& s;
		
		InvalidateF(list<const FAE*>& queue, set<SymState*>& s) : queue(queue), s(s) {}

		void operator()(TraceRecorder::Item* node) {
			SymState* state = STATE_FROM_FAE(*node->fae);
			if (node->queueTag != this->queue.end())
				this->queue.erase(node->queueTag);
//			state->invalidate(this->queue, node->fae);
			if (state->entryPoint)
				s.insert(state);
		}

	};

	void processState(SymState* state, const FAE* parent) {

		assert(state);
		assert(parent);

		this->currentConf = parent;
		this->currentInsn = *state->insn;

		const cl_location& loc = (*state->insn)->loc;
		CL_CDEBUG(loc << ' ' << **state->insn);
		CL_CDEBUG("processing " << parent);
		CL_CDEBUG(std::endl << SymCtx::Dump(*state->ctx, *parent));
		CL_CDEBUG(std::endl << *parent);

		this->execInsn(state, parent);

	}

	void processItem(const FAE* fae) {

		assert(fae);

		SymState* state = STATE_FROM_FAE(*fae);
		
		if (state->testInclusion(*fae)) {
			++this->tracesEvaluated;
			CL_CDEBUG("hit");
			this->traceRecorder.destroyBranch(fae, DestroySimpleF());
			return;
		}

		TraceRecorder::Item* item = this->traceRecorder.find(fae);

		item->queueTag = this->queue.end();

		try {

			std::vector<FAE*> tmp;
			ContainerGuard<std::vector<FAE*> > g(tmp);

			const cl_location& loc = (*state->insn)->loc;

			CL_CDEBUG(loc << ' ' << **state->insn);
			CL_CDEBUG("preprocessing " << fae);
			CL_CDEBUG(std::endl << SymCtx::Dump(*state->ctx, *fae));
			CL_CDEBUG(std::endl << *fae);

			state->prepareOperands(tmp, *fae);

			for (std::vector<FAE*>::iterator i = tmp.begin(); i != tmp.end(); ++i)
				this->traceRecorder.add(fae, *i);

			g.release();
			
			for (std::vector<FAE*>::iterator i = tmp.begin(); i != tmp.end(); ++i)
				this->processState(state, *i);

		} catch (const ProgramError& e) {

			CL_CDEBUG(e.what());

			this->printTrace(*fae);

			throw;

			TraceRecorder::Item* item = this->revRun(*fae);

			if (!item)

				throw ProgramError(e.what(), &(*state->insn)->loc);

			CL_DEBUG("spurious counter example ...");

			this->printTrace(*fae);

			throw;

			state = STATE_FROM_FAE(*item->fae);

			assert(state->entryPoint);

			set<SymState*> s;

			this->traceRecorder.invalidateChildren(item, InvalidateF(this->queue, s));

			const FAE* tmp2 = item->fae;
			
			TraceRecorder::Item* parent = item->parent;

			InvalidateF(this->queue, s)(item);

			this->traceRecorder.remove(tmp2);

			assert(parent);

			parent->removeChild(item);

			for (set<SymState*>::iterator i = s.begin(); i != s.end(); ++i) {
				(*i)->recompute();
				CL_CDEBUG("new fixpoint:" << std::endl << (*i)->fwdConf);
			}

			const cl_location& loc = (*state->insn)->loc;

			CL_CDEBUG("adjusting abstraction ... " << ++state->absHeight);
			CL_CDEBUG("resuming execution ... ");
			CL_CDEBUG(loc << ' ' << **state->insn);

			parent->queueTag = this->queue.insert(this->queue.end(), parent->fae);

		}

	}

	void printInfo(const FAE* fae) {
		if (this->dbgFlag) {
			SymState* state = STATE_FROM_FAE(*fae);
			assert(state);
			if (!state->entryPoint)
				return;
			CL_DEBUG(std::endl << SymCtx::Dump(*state->ctx, *fae));
			CL_DEBUG(std::endl << *fae);
			CL_DEBUG("evaluated states: " << this->statesEvaluated << ", evaluated traces: " << this->tracesEvaluated);
			this->dbgFlag = false;
		}
	}

	void mainLoop() {
		while (!this->queue.empty()) {
//			const FAE* fae = this->queue.front();
//			this->queue.pop_front();
			const FAE* fae = this->queue.back();
			this->queue.pop_back();
			++this->statesEvaluated;
			this->printInfo(fae);
			this->processItem(fae);
		}
	}

	void printTrace(const FAE& fae) {

		vector<pair<const FAE*, const CodeStorage::Insn*> > trace;

		TraceRecorder::Item* item = this->traceRecorder.find(&fae);

		SymState* state = NULL;

		while (item) {

			if (item->itemType == tr_item_type::itDenormalize) {
				state = STATE_FROM_FAE(*item->fae);
				trace.push_back(make_pair(item->fae, *state->insn));
			}

			item = item->parent;

		}

		assert(state);

//		trace.push_back(make_pair(item->fae, *state->insn));

		CL_CDEBUG("trace:");

		for (vector<pair<const FAE*, const CodeStorage::Insn*> >::reverse_iterator i = trace.rbegin(); i != trace.rend(); ++i) {
			if (i->second) {
				state = STATE_FROM_FAE(*i->first);
				CL_CDEBUG(std::endl << SymCtx::Dump(*state->ctx, *i->first));
				CL_CDEBUG(std::endl << *i->first);
				CL_NOTE_MSG(i->second->loc, *(i->second));
			}
//			STATE_FROM_FAE(*i->first)->ctx->dumpContext(*i->first);
//			CL_CDEBUG(std::endl << *(i->first));
		}

//		state = STATE_FROM_FAE(fae);
//		CL_CDEBUG(std::endl << SymCtx::Dump(*state->ctx, fae));
//		CL_CDEBUG(std::endl << fae);
//		CL_NOTE_MSG(this->currentInsn->loc, *this->currentInsn);

	}

	TraceRecorder::Item* revRun(const FAE& fae) {

		CL_CDEBUG("reconstructing abstract trace ...");

		vector<pair<const FAE*, const CodeStorage::Insn*> > trace;

		TraceRecorder::Item* item = this->traceRecorder.find(&fae);

		FAE tmp(fae);

		SymState* state = NULL;
		
		while (item->parent) {

			CL_CDEBUG(std::endl << SymCtx::Dump(*STATE_FROM_FAE(*item->fae)->ctx, *item->fae));
			CL_CDEBUG(std::endl << tmp);

			state = STATE_FROM_FAE(*item->parent->fae);

			CL_CDEBUG("rewinding " << (*state->insn)->loc << ' ' << **state->insn);

			switch (item->itemType) {

				case tr_item_type::itDenormalize: {

					CL_CDEBUG("denormalizing " << std::endl << tmp << "with" << std::endl << *item->fae);
					CL_CDEBUG(item->normInfo);

					if (!tmp.denormalize(*item->fae, item->normInfo)) {
						CL_CDEBUG("spurious counter example (denormalization)!" << std::endl << *item->fae);
						return item;
					}

					break;

				}

				case tr_item_type::itReverse: {

					CL_CDEBUG("reversing " << std::endl << tmp << "with" << std::endl << *item->parent->fae);

					if (!tmp.reverse(*item->parent->fae)) {
						CL_CDEBUG("spurious counter example (reversal)!" << std::endl << *item->parent->fae);
						return item;
					}

					FAE::NormInfo normInfo;

					std::set<size_t> s;
					tmp.getNearbyReferences(fae.varGet(ABP_INDEX).d_ref.root, s);
					tmp.normalize(normInfo, s);

					break;

				}

			}

			if (item->itemType == tr_item_type::itDenormalize)
				trace.push_back(make_pair(item->fae, *state->insn));

			item = item->parent;

		}

		assert(state);

//		trace.push_back(make_pair(item->fae, *state->insn));

		CL_CDEBUG("trace:");

		for (vector<pair<const FAE*, const CodeStorage::Insn*> >::reverse_iterator i = trace.rbegin(); i != trace.rend(); ++i) {
			if (i->second)
				CL_NOTE_MSG(i->second->loc, *(i->second));
//			STATE_FROM_FAE(*i->first)->ctx->dumpContext(*i->first);
//			CL_CDEBUG(std::endl << *(i->first));
		}

		CL_NOTE_MSG(this->currentInsn->loc, *this->currentInsn);

		return NULL;

	}

	void loadTypes() {

	    CL_CDEBUG("loading types ...");

		for (CodeStorage::TypeDb::iterator i = this->stor.types.begin(); i != this->stor.types.end(); ++i) {
			if ((*i)->code != cl_type_e::CL_TYPE_STRUCT)
				continue;
			std::string name;
			if ((*i)->name)
				name = std::string((*i)->name);
			else {
				std::ostringstream ss;
				ss << (*i)->uid;
				name = ss.str();
			}
				
			std::vector<size_t> v;
			NodeBuilder::buildNode(v, *i);
			this->boxMan.createTypeInfo((*i)->name, v);
		}

	}

	void printQueue() const {
		for (std::list<const FAE*>::const_iterator i = this->queue.begin(); i != this->queue.end(); ++i)
			std::cerr << **i;
	}

public:

	Engine(const CodeStorage::Storage& stor)
		: stor(stor), taMan(this->taBackend), boxMan(this->taMan), dbgFlag(false) {
		this->loadTypes();
	}

	~Engine() {
		utils::eraseMap(this->stateStore);
		utils::eraseMap(this->ctxStore);
	}

	void loadBoxes(const boost::unordered_map<std::string, std::string>& db) {

	    CL_DEBUG("loading boxes ...");

		for (boost::unordered_map<std::string, std::string>::const_iterator i = db.begin(); i != db.end(); ++i) {
			this->boxes.push_back((const Box*)this->boxMan.loadBox(i->first, db));
			CL_DEBUG(i->first << ':' << std::endl << *(const FA*)this->boxes.back());
		}

		this->boxMan.buildBoxHierarchy(this->hierarchy, this->basicBoxes);
		
	}

	void run(const CodeStorage::Fnc& main) {

	    CL_CDEBUG("calculating loop entry points ...");
		// compute loop entry points
		this->loopAnalyser.init(main.cfg.entry());
		
	    CL_CDEBUG("creating main context ...");
		// create main context
		SymCtx* mainCtx = this->getCtx(&main);

	    CL_CDEBUG("creating initial state ...");
		// create an initial state
		SymState* init = this->getState(main.cfg.entry()->begin(), mainCtx);

	    CL_CDEBUG("creating empty heap ...");
		// create empty heap with no local variables
		FAE fae(this->taMan, this->boxMan);

	    CL_CDEBUG("allocating global registers ...");
		// add global registers
		SymCtx::init(fae);

	    CL_CDEBUG("entering main stack frame ...");
		// enter main stack frame
		mainCtx->createStackFrame(fae, init);

	    CL_CDEBUG("sheduling initial state ...");
		// schedule initial state for processing
		this->queue.push_back(new FAE(fae));

		this->traceRecorder.init(this->queue.front(), this->queue.begin());

		this->statesEvaluated = 0;
		this->tracesEvaluated = 0;

		try {

			this->mainLoop();

			for (state_store_type::iterator i = this->stateStore.begin(); i != this->stateStore.end(); ++i) {
				if (!i->second->entryPoint)
					continue;
				CL_DEBUG("fixpoint at " << (*i->second->insn)->loc);
				CL_DEBUG(std::endl << i->second->fwdConf);
//				Index<size_t> index;
//				i->second->fwdConf.buildStateIndex(index);
//				std::cerr << index << std::endl;
//				vector<vector<bool> > rel;
//				i->second->fwdConf.downwardSimulation(rel, index);
//				utils::relPrint(std::cerr, rel);
//				TA<label_type> ta(this->taBackend);
//				i->second->fwdConf.minimized(ta);
//				std::cerr << ta;
			}				

			CL_DEBUG("evaluated states: " << this->statesEvaluated << ", evaluated traces: " << this->tracesEvaluated);

		} catch (std::exception& e) {
			CL_CDEBUG(e.what());
			throw;
		}
		
	}

	void setDbgFlag() {
		this->dbgFlag = 1;
	}	

};

SymExec::SymExec(const CodeStorage::Storage &stor)
	: engine(new Engine(stor)) {}

SymExec::~SymExec() {
	delete this->engine;
}

void SymExec::loadBoxes(const boost::unordered_map<std::string, std::string>& db) {
	this->engine->loadBoxes(db);
}

void SymExec::run(const CodeStorage::Fnc& main) {
	this->engine->run(main);
}

void SymExec::setDbgFlag() {
	this->engine->setDbgFlag();
}
