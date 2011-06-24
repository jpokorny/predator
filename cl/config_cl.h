/*
 * Copyright (C) 2009-2010 Kamil Dudka <kdudka@redhat.com>
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

/**
 * @file config_cl.h
 * various compile-time options
 */

#define GIT_SHA1 cl_git_sha1
#include "trap.h"

/**
 * if 1, check each code_listener filter by the integrity checker
 */
#define CL_DEBUG_CLF                    0

/**
 * debug level of the CFG loop scanner
 * - 0 ... print only time elapsed
 * - 1 ... print edges marked as loop-closing
 * - 2 ... print some basic progress info
 */
#define CL_DEBUG_LOOP_SCAN              1

/**
 * debug level of the verbose output when killing variables
 * - 0 ... print only time elapsed
 * - 1 ... print variables being killed and variables black-listed for killing
 * - 2 ... print some basic progress info
 * - 3 ... debug the algorithm at the level of instructions
 * - 4 ... debug the algorithm at the level of operands
 */
#define CL_DEBUG_VAR_KILLER             1

/**
 * if 1, show the amount of time taken by the analysis even without verbose mode
 */
#define CL_EASY_TIMER                   1

/**
 * if 1, filter out repeated error/warning messages (sort of 2>&1 | uniq)
 */
#define CL_MSG_SQUEEZE_REPEATS          1

/**
 * if 1, do not check for unused local variables and registers
 */
#define CLF_BYPASS_USAGE_CHK            1

/**
 * if 1, suppress warnings about unhandled code constructs
 */
#define CLPLUG_SILENT                   0

/**
 * if 1, print progress of code_listener factory when running in verbose mode
 */
#define DEBUG_CL_FACTORY                0
