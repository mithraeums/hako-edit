#!/bin/sh
# comment
echo "home is $HOME and ${PATH}" 'raw $x' a#b $(date) # tail
if [ -n "$1" ]; then exit 2; fi
