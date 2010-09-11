/*
 * Copyright (C) 2010 Kamil Dudka <kdudka@redhat.com>
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

#ifndef H_GUARD_CLUTIL_H
#define H_GUARD_CLUTIL_H

/**
 * @file clutil.hh
 * some generic utilities working on top of code_listener/CodeStorage
 */

#include "code_listener.h"

#include <cassert>
#include <stack>

/// return type of the @b target object that the pointer type can point to
const struct cl_type* targetTypeOfPtr(const struct cl_type *clt);

/**
 * return true if there is any CL_ACCESSOR_REF in the given chain of accessors
 * @note CL_ACCESSOR_REF accessors can't be chained with each other, as it makes
 * semantically no sense
 */
bool seekRefAccessor(const struct cl_accessor *ac);

/// return integral value from the integral constant given as operand
int intCstFromOperand(const struct cl_operand *op);

/// return unique ID of the variable/register given as operand
int varIdFromOperand(const struct cl_operand *op, const char **pName = 0);

// TODO: define TFieldIdxChain within CodeStorage to avoid the template arg
template <class TFieldIdxChain>
struct CltStackItem {
    const struct cl_type    *clt;
    TFieldIdxChain          ic;
};

// take the given visitor through a composite type (or whatever you pass in)
template <class TFieldIdxChain, class TVisitor>
bool /* complete */ traverseTypeIc(const struct cl_type *clt, TVisitor &visitor,
                                   bool digOnlyStructs = false)
{
    assert(clt);

    // initialize DFS
    typedef CltStackItem<TFieldIdxChain> TItem;
    TItem si;
    si.clt = clt;
    si.ic.push_back(0);

    // DFS loop
    std::stack<TItem> todo;
    todo.push(si);
    while (!todo.empty()) {
        TItem &si = todo.top();
        SE_BREAK_IF(si.ic.empty());

        typename TFieldIdxChain::reference &nth = si.ic.back();
        if (nth == si.clt->item_cnt) {
            // done at this level
            todo.pop();
            continue;
        }

        if (digOnlyStructs && CL_TYPE_STRUCT != si.clt->code) {
            // caller is interested only in CL_TYPE_STRUCT, skip this
            ++nth;
            continue;
        }

        const struct cl_type_item *item = si.clt->items + nth;
        const TFieldIdxChain &icConst = si.ic;
        if (!/* continue */visitor(icConst, item))
            return false;

        if (!item->type->item_cnt) {
            // non-coposite type item
            ++nth;
            continue;
        }

        // nest into sub-type
        TItem next;
        next.clt = item->type;
        next.ic = si.ic;
        next.ic.push_back(0);
        todo.push(next);

        // move to the next at this level
        ++nth;
    }

    // the traversal is done, without any interruption by visitor
    return true;
}

#endif /* H_GUARD_CLUTIL_H */