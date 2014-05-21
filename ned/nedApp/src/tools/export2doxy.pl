#!/usr/bin/perl -s

# Parse lines like this and transform them into EPICS records:
# createConfigParam("LvdsRxNoEr5", 'E', 0x0,  1, 16, 0); // LVDS ignore errors (0=discard erronous packet,1=keep all packets)
# createStatusParam("UartByteErr", 0x0,  1, 29); // UART: Byte error              (0=no error,1=error)
# The name and description are truncd to match EPICS string specifications

if (!defined $input_file) {
    print { STDERR } "Usage: $0 -input_file=<input file>\n";
    exit 1;
}
my $class=$input_file;
if ($input_file =~ m/^([.\.]*)/) {
    $class=$1;
}

print <<EOF;
/**
 * \\${class} DspPlugin
EOF

print <<EOF;
 *
 * Following parameters describe the DSP status:
 * Parameter name | asyn type      | DSP cfg register  | Description |
 * -------------- | -------------- | ----------------- | ----------- |
EOF
open (INFILE, $input_file);
foreach $line ( <INFILE> ) {
    chomp($line);
    if ($line =~ m/createStatusParam *\( *"([a-zA-Z0-9_]+)" *, *([0-9xX]+) *, *([0-9]+) *, *([0-9]+).*\/\/ *(.*)$/) {
        my ($name,$offset,$width,$shift,$comment) = ($1,$2,$3,$4,$5);
        my $reg = sprintf("0x%X %d-%d", $offset, $shift, $shift+$width-1);
        printf (" * %-14s | %-14s | %-17s | %s\n", $name, "asynParamInt32", $reg, $comment);
    }
}
close (INFILE);

print <<EOF;
 *
 * Following parameters describe the DSP configuration:
 * Parameter name | asyn type      | init val | DSP cfg register | Description |
 * -------------- | -------------- | -------- | ---------------- | ----------- |
EOF
open (INFILE, $input_file);
foreach $line ( <INFILE> ) {
    chomp($line);
    if ($line =~ m/createConfigParam *\( *"([a-zA-Z0-9_]+)" *, *'([0-9A-F])' *, *([0-9A-FxX]+) *, *([0-9]+) *, *([0-9]+) *, *([0-9]+).*\/\/ *(.*)$/) {
        my ($name,$section,$offset,$width,$shift,$val,$comment) = ($1,$2,$3,$4,$5,$6,$7);
        my $reg = sprintf("%s 0x%X %d-%d", $section, $offset, $shift, $shift+$width-1);
        printf (" * %-14s | %-14s | %8d | %-17s | %s\n", $name, "asynParamInt32", $val, $reg, $comment);
    }

}
close (INFILE);

print <<EOF;
 */
EOF
