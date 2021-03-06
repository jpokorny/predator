/*
 * Copyright (C) 2010 Jiri Simacek
 *
 * This file is part of forester.
 *
 * forester is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * any later version.
 *
 * forester is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with forester.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef BOX_MANAGER_H
#define BOX_MANAGER_H

#include <vector>
#include <unordered_map>
#include <string>
#include <sstream>
#include <fstream>

#include <unordered_map>

#include <boost/unordered_map.hpp>
#include <boost/algorithm/string.hpp>

#include "config.h"

#include "treeaut.hh"
#include "tatimint.hh"
#include "label.hh"
#include "types.hh"
#include "box.hh"
#include "utils.hh"
#include "restart_request.hh"

class BoxAntichain {

	std::unordered_map<Box::Signature, std::list<Box>, boost::hash<Box::Signature>> boxes_;

	std::list<Box> obsolete_;

	bool modified_;

	size_t size_;

public:

	BoxAntichain() : boxes_(), obsolete_(), modified_(false), size_(0) {}

	const Box* get(const Box& box) {

		this->modified_ = false;

		auto p = this->boxes_.insert(std::make_pair(box.getSignature(), std::list<Box>()));

		if (!p.second) {

			for (auto iter = p.first->second.begin(); iter != p.first->second.end(); ) {

				assert(!this->modified_ || !box.simplifiedLessThan(*iter));

				if (!this->modified_ && box.simplifiedLessThan(*iter))
					return &*iter;

				if (iter->simplifiedLessThan(box)) {

					auto tmp = iter++;

					this->obsolete_.splice(this->obsolete_.end(), p.first->second, tmp);

					this->modified_ = true;

				} else {

					++iter;

				}

			}

		}

		p.first->second.push_back(box);

		this->modified_ = true;

		++this->size_;

		return &p.first->second.back();

	}

	const Box* lookup(const Box& box) const {

		auto iter = this->boxes_.find(box.getSignature());

		if (iter != this->boxes_.end()) {

			for (auto& box2 : iter->second) {

				if (box.simplifiedLessThan(box2))
					return &box2;

			}

		}

		return nullptr;

	}

	bool modified() const {
		return this->modified_;
	}

	size_t size() const {
		return this->size_;
	}

	void clear() {
		this->boxes_.clear();
	}

	void asVector(std::vector<const Box*>& boxes) const {

		for (auto& signatureListPair : this->boxes_) {

			for (auto& box : signatureListPair.second)
				boxes.push_back(&box);

		}

	}

};

class BoxSet {

	std::unordered_set<Box, boost::hash<Box>> boxes_;

	bool modified_;

public:

	BoxSet() : boxes_(), modified_(false) {}

	const Box* get(const Box& box) {

		auto p = this->boxes_.insert(box);

		this->modified_ = p.second;

		return &*p.first;

	}

	const Box* lookup(const Box& box) const {

		auto iter = this->boxes_.find(box);

		return (iter == this->boxes_.end())?(nullptr):(&*iter);

	}

	bool modified() const {
		return this->modified_;
	}

	void clear() {
		this->boxes_.clear();
	}

	size_t size() const {
		return this->boxes_.size();
	}

	void asVector(std::vector<const Box*>& boxes) const {

		for (auto& box : this->boxes_)
			boxes.push_back(&box);

	}

};

#if FA_BOX_APPROXIMATION
	typedef BoxAntichain BoxDatabase;
#else
	typedef BoxSet BoxDatabase;
#endif

class BoxMan {

	boost::unordered_map<Data, NodeLabel*> dataStore;
	std::vector<const Data*> dataIndex;
	boost::unordered_map<std::vector<const AbstractBox*>, NodeLabel*> nodeStore;
	boost::unordered_set<std::pair<const TypeBox*, std::vector<size_t> > > tagStore;
	boost::unordered_map<std::pair<size_t, std::vector<Data> >, NodeLabel*> vDataStore;

	boost::unordered_map<SelData, const SelBox*> selIndex;
	boost::unordered_map<std::string, const TypeBox*> typeIndex;

	BoxDatabase boxes;

	const std::pair<const Data, NodeLabel*>& insertData(const Data& data) {
		std::pair<boost::unordered_map<Data, NodeLabel*>::iterator, bool> p
			= this->dataStore.insert(std::make_pair(data, (NodeLabel*)NULL));
		if (p.second) {
			p.first->second = new NodeLabel(&p.first->first, this->dataIndex.size());
			this->dataIndex.push_back(&p.first->first);
		}
		return *p.first;
	}

	std::string getBoxName() const {

		assert(this->boxes.size());

		std::stringstream sstr;

		sstr << "box" << this->boxes.size() - 1;

		return sstr.str();

	}

public:

	label_type lookupLabel(const Data& data) {
		return this->insertData(data).second;
	}

	label_type lookupLabel(size_t arity, const std::vector<Data>& x) {
		std::pair<boost::unordered_map<std::pair<size_t, std::vector<Data> >, NodeLabel*>::iterator, bool> p
			= this->vDataStore.insert(std::make_pair(std::make_pair(arity, x), (NodeLabel*)NULL));
		if (p.second)
			p.first->second = new NodeLabel(&p.first->first.second);
		return p.first->second;
	}

	struct EvaluateBoxF {
		NodeLabel& label;
		std::vector<size_t>& tag;
		EvaluateBoxF(NodeLabel& label, std::vector<size_t>& tag) : label(label), tag(tag) {}
		bool operator()(const AbstractBox* aBox, size_t index, size_t offset) {
			switch (aBox->getType()) {
				case box_type_e::bSel: {
					const SelBox* sBox = (const SelBox*)aBox;
					this->label.addMapItem(sBox->getData().offset, aBox, index, offset);
					this->tag.push_back(sBox->getData().offset);
					break;
				}
				case box_type_e::bBox: {
					const Box* bBox = (const Box*)aBox;
					for (std::set<size_t>::const_iterator i= bBox->outputCoverage().begin(); i != bBox->outputCoverage().end(); ++i) {
						this->label.addMapItem(*i, aBox, index, offset);
						this->tag.push_back(*i);
					}
					break;
				}
				case box_type_e::bTypeInfo:
					this->label.addMapItem((size_t)(-1), aBox, index, (size_t)(-1));
					break;
				default:
					assert(false);
					break;
			}
			return true;
		}
	};

	label_type lookupLabel(const std::vector<const AbstractBox*>& x) {

		std::pair<boost::unordered_map<std::vector<const AbstractBox*>, NodeLabel*>::iterator, bool> p
			= this->nodeStore.insert(std::make_pair(x, (NodeLabel*)NULL));

		if (p.second) {

			NodeLabel* label = new NodeLabel(&p.first->first);

			std::vector<size_t> tag;

			label->iterate(EvaluateBoxF(*label, tag));

			std::sort(tag.begin(), tag.end());

			label->setTag(
				(void*)&*this->tagStore.insert(
					std::make_pair((const TypeBox*)label->boxLookup((size_t)(-1), NULL), tag)
				).first
			);

			p.first->second = label;

		}

		return p.first->second;

	}

	const Data& getData(const Data& data) {
		return this->insertData(data).first;
	}

	size_t getDataId(const Data& data) {
		return this->insertData(data).second->getDataId();
	}

	const Data& getData(size_t index) const {
		assert(index < this->dataIndex.size());
		return *this->dataIndex[index];
	}

	const SelBox* getSelector(const SelData& sel) {
		std::pair<const SelData, const SelBox*>& p = *this->selIndex.insert(
			std::make_pair(sel, (const SelBox*)NULL)
		).first;
		if (!p.second)
			p.second = new SelBox(&p.first);
		return p.second;
	}

	const TypeBox* getTypeInfo(const std::string& name) {
		boost::unordered_map<std::string, const TypeBox*>::const_iterator i = this->typeIndex.find(name);
		if (i == this->typeIndex.end())
			throw std::runtime_error("BoxMan::getTypeInfo(): type for " + name + " not found!");
		return i->second;
	}

	const TypeBox* createTypeInfo(const std::string& name, const std::vector<size_t>& selectors) {
		std::pair<const std::string, const TypeBox*>& p = *this->typeIndex.insert(
			std::make_pair(name, (const TypeBox*)NULL)
		).first;
		if (p.second)
			throw std::runtime_error("BoxMan::createTypeInfo(): type already exists!");
		p.second = new TypeBox(name, selectors);
		return p.second;
	}

	static size_t translateSignature(ConnectionGraph::CutpointSignature& result,
		std::vector<std::pair<size_t, size_t>>& selectors, size_t root,
		const ConnectionGraph::CutpointSignature& signature, size_t aux,
		const std::vector<size_t>& index) {

		size_t auxSelector = (size_t)(-1);

		for (auto& cutpoint : signature) {

			assert(cutpoint.root < index.size());
			assert(cutpoint.fwdSelectors.size());

			result.push_back(cutpoint);
			result.back().root = index[cutpoint.root];

			if (cutpoint.root == aux)
				auxSelector = *cutpoint.fwdSelectors.begin();

			if (cutpoint.root != root) {

				selectors.push_back(
					std::make_pair(*cutpoint.fwdSelectors.begin(), cutpoint.bwdSelector)
				);

			}

		}

		return auxSelector;

	}

	Box* createType1Box(size_t root, const std::shared_ptr<TA<label_type>>& output,
		const ConnectionGraph::CutpointSignature& signature, std::vector<size_t>& inputMap,
		const std::vector<size_t>& index) {

		ConnectionGraph::CutpointSignature outputSignature;
		std::vector<std::pair<size_t, size_t>> selectors;

		BoxMan::translateSignature(
			outputSignature, selectors, root, signature, (size_t)(-1), index
		);

		return new Box(
			"",
			output,
			outputSignature,
			inputMap,
			std::shared_ptr<TA<label_type>>(nullptr),
			0,
			ConnectionGraph::CutpointSignature(),
			selectors
		);

	}

	Box* createType2Box(size_t root, const std::shared_ptr<TA<label_type>>& output,
		const ConnectionGraph::CutpointSignature& signature, std::vector<size_t>& inputMap,
		size_t aux, const std::shared_ptr<TA<label_type>>& input,
		const ConnectionGraph::CutpointSignature& signature2, size_t inputSelector,
		std::vector<size_t>& index) {

		assert(aux < index.size());
		assert(index[aux] >= 1);

		ConnectionGraph::CutpointSignature outputSignature, inputSignature;
		std::vector<std::pair<size_t, size_t>> selectors;

		size_t auxSelector = BoxMan::translateSignature(
			outputSignature, selectors, root, signature, aux, index
		);

		size_t start = selectors.size();

		for (auto& cutpoint : signature2) {

			assert(cutpoint.root < index.size());

			if (index[cutpoint.root] == (size_t)(-1)) {

				index[cutpoint.root] = start++;

				selectors.push_back(std::make_pair(auxSelector, (size_t)(-1)));

			}

			inputSignature.push_back(cutpoint);
			inputSignature.back().root = index[cutpoint.root];

		}

		size_t inputIndex = index[aux] - 1;

		assert(inputIndex < selectors.size());

		if (selectors[inputIndex].second > inputSelector)
			selectors[inputIndex].second = inputSelector;

		return new Box(
			"",
			output,
			outputSignature,
			inputMap,
			input,
			inputIndex,
			inputSignature,
			selectors
		);

	}

	const Box* getBox(const Box& box) {

		auto cpBox = this->boxes.get(box);

		if (this->boxes.modified()) {

			Box* pBox = const_cast<Box*>(cpBox);

			pBox->name = this->getBoxName();
			pBox->initialize();

			CL_CDEBUG(1, "learning " << *(AbstractBox*)cpBox << ':' << std::endl << *cpBox);

#if FA_RESTART_AFTER_BOX_DISCOVERY
			throw RestartRequest("a new box encountered");
#endif
		}

		return cpBox;

	}

	const Box* lookupBox(const Box& box) const {

		return this->boxes.lookup(box);

	}

public:

	BoxMan() {}

	~BoxMan() { this->clear(); }

	void clear() {

		utils::eraseMap(this->dataStore);
		this->dataIndex.clear();
		utils::eraseMap(this->nodeStore);
		this->tagStore.clear();
		utils::eraseMap(this->vDataStore);
		utils::eraseMap(this->selIndex);
		utils::eraseMap(this->typeIndex);
		this->boxes.clear();

	}

	const BoxDatabase& boxDatabase() const {

		return this->boxes;

	}

};

#endif
