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

#ifndef UFAE_H
#define UFAE_H

#include <vector>

#include "utils.hh"
#include "treeaut.hh"
#include "labman.hh"
#include "forestautext.hh"

class UFAE {
	
	TA<FA::label_type>& backend;
	
	size_t stateOffset;
	
	mutable LabMan& labMan;
	
public:

	UFAE(TA<FA::label_type>& backend, LabMan& labMan) : backend(backend), stateOffset(1), labMan(labMan) {
		// let 0 be the only accepting state
		this->backend.addFinalState(0);
	}

	TA<FA::label_type>& fae2ta(TA<FA::label_type>& dst, Index<size_t>& index, const FAE& src) {
		vector<size_t> lhs;
		dst.clear();
		for (vector<TA<FA::label_type>*>::const_iterator i = src.roots.begin(); i != src.roots.end(); ++i) {
			TA<FA::label_type>::reduce(dst, **i, index, this->stateOffset, false);
			lhs.push_back(index[(*i)->getFinalState()]);
		}
		dst.addTransition(lhs, &labMan.lookup(src.variables, lhs.size()), 0);
		dst.addFinalState(0);
		return dst;
	}

	void join(const TA<FA::label_type>& src, const Index<size_t>& index) {
		TA<FA::label_type>::disjointUnion(this->backend, src, false);
		this->stateOffset += index.size();
	}

	void ta2fae(vector<FAE*>& dst, TAManager<FA::label_type>& taMan, LabMan& labMan, BoxManager& boxMan) const {
		TA<FA::label_type>::dfs_cache_type dfsCache;
		this->backend.buildDFSCache(dfsCache);
		vector<const TT<FA::label_type>*>& v = dfsCache.insert(make_pair(0, vector<const TT<FA::label_type>*>())).first->second;
		// iterate over all "synthetic" transitions and constuct new FAE for each
		for (vector<const TT<FA::label_type>*>::iterator i = v.begin(); i != v.end(); ++i) {
			FAE* fae = new FAE(taMan, labMan, boxMan);
			Guard<FAE> guard(fae);
			fae->loadTA(this->backend, dfsCache, *i, this->stateOffset);
			dst.push_back(fae);
			guard.release();
		}
	}

};

#endif