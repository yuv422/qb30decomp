#!/usr/bin/perl
# Normalises decompiled BASIC for comparing two decompilations: variables,
# labels, functions and SUBs are renamed by order of first appearance and
# comment lines are dropped. Temporaries the decompiler introduces (tmpN) are
# ordinary variables when the program is decompiled again, so they count as
# variables.
#
# usage: normalize.pl FILE.BAS
use strict;
use warnings;

my %m;
my %n;
while (my $line = <>) {
    next if $line =~ /^'/;
    $line =~ s/\r//;
    $line =~ s/\btmp(\d+)!/tmp$1/g;
    $line =~ s{\b(?:(vari|loc|FNf|sub)([0-9A-F]{4})|tmp(\d+))\b}{
        my ($p, $k) = defined $1 ? ($1, "$1$2") : ("vari", "tmp$3");
        $m{$k} //= $p . (++$n{$p});
        $m{$k}
    }ge;
    print $line;
}
