#!/bin/sh

[ "x$1" = "x" ] && echo "Usage: $0 <filename>" && exit 1
[ ! -f $1 ] && echo "ERROR: '$1' does not exist or is not a file" && exit 1

cat <<EOF
/**
 * \class DspPlugin
 *
 * Following parameters describe the DSP configuration:
 * Parameter name | asyn type | init val | DSP cfg register | Description |
 * -------------- | --------- | -------- | ---------------- | ----------- |
EOF

egrep "^[ \t]*createConfigParam" $1 | tr -s " " | \
    sed -e 's/.*createConfigParam *( *//' \
        -e 's/^\"\([^"]*\)\"/\1/' \
        -e 's/);//' \
        -e "s/'//2" -e "s/'//1" \
        -e 's/ *, */!/' -e 's/ *, */!/' -e 's/ *, */!/' -e 's/ *, */!/' -e 's/ *, */!/' \
        -e 's# *// *#!#' | \
    awk -F! '{ print $1 "!asynParamInt32!" $6 "!" $2 " " $3 " " $5 "-" $4+$5 "!" $7 }' | \
    sed -e 's/! *$/! TODO Missing description/' \
        -e 's/!/ \| /g' -e 's/^/ * /'

cat <<EOF
 *
 * Following parameters describe the DSP configuration:
 * Parameter name | asyn type | DSP cfg register | Description |
 * -------------- | --------- | ---------------- | ----------- |
EOF
egrep "^[ \t]*createStatusParam\(" $1 | tr -s " " | \
    sed -e 's/.*createStatusParam *( *//' \
        -e 's/^\"\([^"]*\)\"/\1/' \
        -e 's/);//' \
        -e 's/ *, */!/' -e 's/ *, */!/' -e 's/ *, */!/' -e 's/ *, */!/' -e 's/ *, */!/' \
        -e 's# *// *#!#' | \
    awk -F! '{ print $1 "!asynParamInt32!" $2 " " $4 "-" $3+$4 "!" $5 }' | \
    sed -e 's/! *$/! TODO Missing description/' \
        -e 's/!/ \| /g' -e 's/^/ * /'
cat <<EOF
 */
EOF
