#!/usr/bin/perl -w
# Written by: Andras Fekete (C) 2016
# This program generates using gnuplot a listing of what kinds of
# transactions occurred on the disks from the UMASS Financial and
# WebSearch trace files.

# Set these equal to desired starting and ending times
my $sTime;
my $eTime;

####################################################################
## Edit below here only if you know what you're doing
use strict;
use autodie qw(:all);
use File::Basename;

if(!defined($sTime)) { $sTime = ""; }
if(!defined($eTime)) { $eTime = ""; }

# Generate table that shows the speedup
foreach my $file(<"*.spc">) {
	my ($title,$dir,$ext) = fileparse($file, qr/\.[^.]*/);
	my $PLOT;
	open $PLOT, '|-','gnuplot';
	print $PLOT <<EOF;
		set term png font ",24"
		set datafile separator ","
		set key off
		set boxwidth 0.5
		set style fill solid 1.0 border -1
		set xlabel "Time (s)"
		set ytics offset 1
		set xtics rotate by -45 font ",20"
		set xrange [$sTime:$eTime]
EOF
	if(!-e "$file-ArrOffset.png") {
		print "Creating $file-ArrOffset.png...\n";
		print $PLOT <<EOF;
			set output '$file-ArrOffset.png'
			set title "$title - Array Offset"
			stats '$file' using 2
			maxOff = STATS_max
			set ylabel "Location on disk" offset 3
			plot '$file' using 5:(\$2+maxOff*\$1) t "Offset" w lines
EOF
	}
	if(!-e "$file-Offset.png") {
		print "Creating $file-Offset.png...\n";
		print $PLOT <<EOF;
			set output '$file-Offset.png'
			set title "$title - Per Disk Offset"
			set ylabel "Location on disk" offset 3
			plot '$file' every ::1 using 5:2 t "Offset" w lines
EOF
	}
	if(!-e "$file-Size.png") {
		print "Creating $file-Size.png...\n";
		print $PLOT <<EOF;
			set output '$file-Size.png'
			set title "$title - Per Disk Size"
			set ylabel "Byte size of TX" offset 3
			plot '$file' every ::1 using 5:3 t "Size" w points
EOF
	}
	close $PLOT;
}

