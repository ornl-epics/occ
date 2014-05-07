#!/usr/bin/perl -s

# Parse lines like this and transform them into EPICS records:
# createConfigParam("LvdsRxNoEr5", 'E', 0x0,  1, 16, 0); // LVDS ignore errors (0=discard erronous packet,1=keep all packets)
# The name and description are truncd to match EPICS string specifications

if (!defined $name_prefix || !defined $input_file) {
    print { STDERR } "Usage: $0 -name_prefix=<BLXXX:Det:RocYYY:> -input_file=<input file>\n";
    exit 1;
}

my $MAX_NAME_LEN      = 29 - length($name_prefix);
my $MAX_DESC_LEN      = 29;
my $MAX_MBBO_xNAM_LEN = 16;
my $MAX_BO_xNAM_LEN   = 20;

my @vals = ("ZRVL","ONVL","TWVL","THVL","FRVL","FVVL","SXVL","SVVL","EIVL","NIVL","TEVL","ELVL","TVVL","TTVL","FTVL","FFVL");
my @nams = ("ZRST","ONST","TWST","THST","FRST","FVST","SXST","SVST","EIST","NIST","TEST","ELST","TVST","TTST","FTST","FFST");

sub  trim { my $s = shift; $s =~ s/^\s+|\s+$//g; return $s };
sub trunc {
    my ($str, $max_len, $record_name, $field) = @_;
    $str = trim($str);
    if (length($str) > $max_len) {
        print { STDERR } ("WARN: Truncating $record_name record $field to $max_len chars\n");
    }
    return substr($str, 0, $max_len);
}

open (INFILE, $input_file);
foreach $line ( <INFILE> ) {
    chomp($line);
    if ($line =~ m/createConfigParam *\( *"([a-zA-Z0-9_]+)" *, *'([0-9A-F])' *, *([0-9xX]+) *, *([0-9]+) *, *([0-9]+) *, *([0-9]+).*\/\/ *(.*)$/) {
        my ($name,$section,$offset,$width,$shift,$val,$comment) = ($1,$2,$3,$4,$5,$6,$7);
        $comment =~ /^\s*([^\(]*)\(?(.*)\)?$/;
        my ($desc, $valstr) = ($1, $2);
        $valstr =~ s/\)$//;

        $name = trunc($name, $MAX_NAME_LEN, $name, "name");
        $desc = trunc($desc, $MAX_DESC_LEN, $name, "DESC");
       
        if ($valstr =~ /^range/) {
            print ("record(longout, \"\$(P)\$(R)$name\")\n");
            print ("\{\n");
            print ("    field(DESC, \"$desc\")\n");
            print ("    field(DTYP, \"asynInt32\")\n");
            print ("    field(OUT,  \"\@asyn(\$(PORT),\$(ADDR),\$(TIMEOUT))$name\")\n");
	    print ("    field(SCAN, \"I/O Intr\")\n");
	    print ("    field(VAL,  \"$val\")\n");
            print ("\}\n");
        } elsif ($width == 1) {
            print ("record(bo, \"\$(P)\$(R)$name\")\n");
            print ("\{\n");
            print ("    field(DESC, \"$desc\")\n");
            print ("    field(DTYP, \"asynInt32\")\n");
            print ("    field(OUT,  \"\@asyn(\$(PORT),\$(ADDR),\$(TIMEOUT))$name\")\n");
            print ("    field(SCAN, \"I/O Intr\")\n");
	    print ("    field(VAL,  \"$val\")\n");
            if ($valstr =~ m/([0-9]) *= *([^,]+), *([0-9]) *= *(.+)/) {
                my ($zval,$znam,$oval,$onam) = ($1,$2,$3,$4);
                if ($zval != 0) { my $temp=$znam; $znam=$onam; $onam=$temp; }
                $znam = trunc($znam, $MAX_BO_xNAM_LEN, $name, "ZNAM");
                $onam = trunc($onam, $MAX_BO_xNAM_LEN, $name, "ONAM");
                print ("    field(ZNAM, \"$znam\")\n");
                print ("    field(ONAM, \"$onam\")\n");
            }
            print ("\}\n");
        } elsif ($width > 1 && $width < 15 && $valstr ne "") {
            print ("record(mbbo, \"\$(P)\$(R)$name\")\n");
            print ("\{\n");
            print ("    field(DESC, \"$desc\")\n");
            print ("    field(DTYP, \"asynInt32\")\n");
            print ("    field(OUT,  \"\@asyn(\$(PORT),\$(ADDR),\$(TIMEOUT))$name\")\n");
            print ("    field(SCAN, \"I/O Intr\")\n");
	    print ("    field(VAL,  \"$val\")\n");
            foreach (split(',', $valstr)) {
                my ($xval,$xnam) = split(/=/, $_);
                $xnam = trunc($xnam, $MAX_MBBO_xNAM_LEN, $name, $nams[$i]);
                print ("    field($vals[$i], \"$xval\")\n");
                print ("    field($nams[$i], \"$xnam\")\n");
            }
            print ("\}\n");
        } else {
            print ("record(longout, \"\$(P)\$(R)$name\")\n");
            print ("\{\n");
            print ("    field(DESC, \"$desc\")\n");
            print ("    field(DTYP, \"asynInt32\")\n");
            print ("    field(OUT,  \"\@asyn(\$(PORT),\$(ADDR),\$(TIMEOUT))$name\")\n");
            print ("    field(SCAN, \"I/O Intr\")\n");
	    print ("    field(VAL,  \"$val\")\n");
            print ("\}\n");
        }
    }
}
close (INFILE);

