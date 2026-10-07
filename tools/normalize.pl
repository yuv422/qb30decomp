#!/usr/bin/perl
# Normalises decompiled BASIC for comparing two decompilations: variables,
# labels, functions and SUBs are renamed by order of first appearance and
# comment lines are dropped.
#
# usage: normalize.pl FILE.BAS
use strict;
use warnings;

my %m;
my %n;
while (my $line = <>) {
    next if $line =~ /^'/;
    $line =~ s/\r//;
    $line =~ s{\b(vari|loc|FNf|sub)([0-9A-F]{4})\b}{
        my $k = "$1$2";
        $m{$k} //= $1 . (++$n{$1});
        $m{$k}
    }ge;
    print $line;
}
