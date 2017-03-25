#!/bin/bash

VALGRIND=$(command -v valgrind)
if [ ! $VALGRIND ]
then
    echo "ERROR: Valgrind is required for this test." >&2
    exit 1
fi

if [ $(dirname $0) == "." ]
then
    TESTS="../build/tests"
else
    TESTS="build/tests"
fi

if [ -f $TESTS ]
then
    $VALGRIND --leak-check=full $TESTS
else
    echo "ERROR: No \"tests\" executable was found under \"../build\"." >&2
fi
