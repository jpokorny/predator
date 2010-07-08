/*
 * Copyright (C) 2009 Kamil Dudka <kdudka@redhat.com>
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

#include "config.h"
#include "symgc.hh"

#include <cl/cl_msg.hh>
#include <cl/storage.hh>

#include "symheap.hh"
#include "symutil.hh"
#include "worklist.hh"

#include <stack>
#include <vector>

#include <boost/foreach.hpp>

namespace {

template <class TWL>
void digPointingObjects(TWL &wl, const SymHeap &heap, TValueId val) {
    // go through all objects having the value
    SymHeap::TContObj cont;
    heap.usedBy(cont, val);
    BOOST_FOREACH(TObjId obj, cont) {

        // go through all super objects
        while (0 < obj) {
            wl.schedule(obj);
            obj = heap.objParent(obj);
        }
    }
}

bool digJunk(const SymHeap &heap, TValueId *ptrVal) {
    if (*ptrVal <= 0)
        return false;

    const EUnknownValue code = heap.valGetUnknown(*ptrVal);
    switch (code) {
        case UV_KNOWN:
        case UV_ABSTRACT:
            break;

        default:
            return false;
    }

    if (VAL_INVALID != heap.valGetCustom(0, *ptrVal))
        // ignore custom values (e.g. fnc pointers)
        return false;

    TObjId obj = heap.pointsTo(*ptrVal);
    if (!isHeapObject(heap, obj))
        // non-heap object simply can't be JUNK
        return false;

    // only root objects can be destroyed
    digRootObject(heap, ptrVal);

    WorkList<TObjId> wl;
    digPointingObjects(wl, heap, *ptrVal);
    while (wl.next(obj)) {
        if (!isHeapObject(heap, obj))
            return false;

        const TValueId val = heap.placedAt(obj);
        if (val <= 0)
            TRAP;

        digPointingObjects(wl, heap, val);
    }

    return true;
}

} // namespace

bool collectJunk(SymHeap &sh, TValueId val, LocationWriter lw) {
    bool detected = false;

    std::stack<TValueId> todo;
    todo.push(val);
    while (!todo.empty()) {
        TValueId val = todo.top();
        todo.pop();

        if (digJunk(sh, &val)) {
            detected = true;
            const TObjId obj = sh.pointsTo(val);
            if (obj <= 0)
                TRAP;

            // gather all values inside the junk object
            std::vector<TValueId> ptrs;
            getPtrValues(ptrs, sh, obj);

            // destroy junk
            if (lw)
                CL_WARN_MSG(lw, "killing junk");
            sh.objDestroy(obj);

            // schedule just created junk candidates for next wheel
            BOOST_FOREACH(TValueId ptrVal, ptrs) {
                todo.push(ptrVal);
            }
        }
    }

    return detected;
}
