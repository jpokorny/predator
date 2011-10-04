#!/bin/bash
export SELF="$0"
export LC_ALL=C

export MSG_INFLOOP=': warning: end of function .*() has not been reached'
export MSG_LABEL_FOUND=': error: error label "ERROR" has been reached'
export MSG_MEMLEAK=': warning: memory leak detected'
export MSG_OUR_WARNINGS=': warning: .*\[-fplugin-libsl\]$'
export MSG_TIME_ELAPSED=': note: clEasyRun() took '
export MSG_UNHANDLED_CALL=': warning: ignoring call of undefined function: '

die() {
    printf "%s: %s\n" "$SELF" "$*" >&2
    exit 1
}

usage() {
    printf "Usage: %s path/to/test-case.c [-m32|-m64] [CFLAGS]\n\n" "$SELF" >&2
    cat >&2 << EOF
    The verification result (SAFE, UNSAFE, or UNKNOWN) will be printed to
    standard output.  All other information will be printed to standard error
    output.  There is no timeout or ulimit set by this script.  If these
    constraints are violated, it should be treated as UNKNOWN result.  Do not
    forget to use the -m32 option when compiling 32bit preprocessed code on a
    64bit OS.

EOF
    exit 1
}

test -r "$1" || usage

match() {
    line="$1"
    shift
    printf "%s" "$line" | grep "$@" >/dev/null
    return $?
}

# check GCC version
GCC_VER="`gcc --version | head -1`"
match "gcc (GCC) 4.6.1" "$GCC_VER" || die "unexpected GCC version: ${GCC_VER}
*** Please set the environment variables according to README."

fail() {
    # exit now, it makes no sense to continue at this point
    echo UNKNOWN
    exit 1
}

report_unsafe() {
    # drop the remainder of the output
    cat > /dev/null
    echo UNSAFE
    exit 0
}

parse_gcc_output() {
    ERROR_DETECTED=no
    ENDED_GRACEFULLY=no

    while read line; do
        if match "$line" "$MSG_UNHANDLED_CALL"; then
            # call of an external function we have no model for, we have to fail
            fail

        elif match "$line" "$MSG_LABEL_FOUND"; then
            # an ERROR label has been reached
            report_unsafe

        elif match "$line" ": error: "; then
            # errors already reported, better to fail now
            fail

        elif match "$line" -E "$MSG_INFLOOP|$MSG_MEMLEAK"; then
            # memory leakage and infinite loop do not mean UNSAFE, ignore them
            continue

        elif match "$line" "$MSG_OUR_WARNINGS"; then
            # all other warnings treat as errors
            ERROR_DETECTED=yes

        elif match "$line" "$MSG_TIME_ELAPSED"; then
            # we ended up without a crash, yay!
            ENDED_GRACEFULLY=yes
        fi
    done

    if test xyes = "x$ERROR_DETECTED"; then
        fail
    elif test xyes = "x$ENDED_GRACEFULLY"; then
        echo SAFE
    else
        fail
    fi
}

gcc -fplugin="libsl.so"                                 \
    -fplugin-arg-libsl-args="error_label:ERROR"         \
    -o /dev/null -O0 -c "$@" 2>&1                       \
    | tee /dev/fd/2                                     \
    | parse_gcc_output
