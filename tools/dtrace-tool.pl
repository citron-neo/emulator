#!/usr/bin/perl
# SPDX-FileCopyrightText: Copyright 2025 Citron Emulator Project
# SPDX-License-Identifier: GPL-3.0-or-later
# Basic script to run dtrace sampling over the program (requires Flamegraph)
# Usage is either running as: ./dtrace-tool.sh pid (then input the pid of the process)
# Or just run directly with: ./dtrace-tool.sh <command>
use strict;
use warnings;
use POSIX qw(strftime);

my $input;
my $sampling_hz = '4000';
my $sampling_time = '5';
my $sampling_pid = `pgrep citron`;
my $sampling_program = 'citron';
my $sampling_type = 0;

sub dtrace_ask_params {
    my $is_ok = 'Y';
    do {
        print "Sampling HZ [" . $sampling_hz . "]: ";
        chomp($input = <STDIN>);
        $sampling_hz = $input || $sampling_hz;

        print "Sampling time [" . $sampling_time . "]: ";
        chomp($input = <STDIN>);
        $sampling_time = $input || $sampling_time;

        print "Sampling pid [" . $sampling_pid . "]: ";
        chomp($input = <STDIN>);
        $sampling_pid = $input || $sampling_pid;

        print "Are these settings correct?: [" . $is_ok . "]\n";
        print "HZ = " . $sampling_hz . "\nTime = " . $sampling_time . "\nPID = " . $sampling_pid . "\n";
        chomp($input = <STDIN>);
        $is_ok = $input || $is_ok;
    } while ($is_ok eq 'n');
}

sub dtrace_probe_profiling {
    if ($sampling_type eq 0) {
        return "
profile-".$sampling_hz." /pid == ".$sampling_pid." && arg0/ {
    @[stack(100)] = count();
}
profile-".$sampling_hz." /pid == ".$sampling_pid." && arg1/ {
    @[ustack(100)] = count();
}
tick-".$sampling_time."s {
    exit(0);
}";
    } elsif ($sampling_type eq 1) {
        return "
syscall:::entry /pid == ".$sampling_pid."/ {
    \@traces[ustack(100)] = count();
}
tick-".$sampling_time."s {
    exit(0);
}";
    } elsif ($sampling_type eq 2) {
        return "
profile-".$sampling_hz." /pid == ".$sampling_pid." && arg0/ {
    @[stringof(curthread->td_name), stack(100)] = count();
}
profile-".$sampling_hz." /pid == ".$sampling_pid." && arg1/ {
    @[stringof(curthread->td_name), ustack(100)] = count();
}
tick-".$sampling_time."s {
    exit(0);
}";
    } elsif ($sampling_type eq 3) {
        return "
io::start /pid == ".$sampling_pid."/ {
    @[ustack(100)] = count();
}
tick-".$sampling_time."s {
    exit(0);
}";
    } else {
        die "idk";
    }
}

sub dtrace_generate {
    my @date = (localtime(time))[5, 4, 3, 2, 1, 0];
    $date[0] += 1900;
    $date[1]++;
    my $fmt_date = sprintf "%4d-%02d-%02d_%02d-%02d-%02d", @date;
    my $trace_dir = "dtrace-out";
    my $trace_file = $trace_dir . "/" . $fmt_date . ".user_stacks";
    my $trace_fold = $trace_dir . "/" . $fmt_date . ".fold";
    my $trace_svg = $trace_dir . "/" . $fmt_date . ".svg";
    my $trace_probe = dtrace_probe_profiling;

    print $trace_probe . "\n";
    system "sudo", "dtrace", "-Z", "-n", $trace_probe, "-o", $trace_file;
    die "$!" if $?;

    open (my $trace_fold_handle, ">", $trace_fold) or die "$!";
    #run ["perl", "../FlameGraph/stackcollapse.pl", $trace_file], ">", \my $fold_output;
    my $fold_output = `perl ../FlameGraph/stackcollapse.pl $trace_file`;
    print $trace_fold_handle $fold_output;

    open (my $trace_svg_handle, ">", $trace_svg) or die "$!";
    #run ["perl", "../FlameGraph/flamegraph.pl", $trace_fold], ">", \my $svg_output;
    my $svg_output = `perl ../FlameGraph/flamegraph.pl $trace_fold`;
    print $trace_svg_handle $svg_output;

    system "sudo", "chmod", "0666", $trace_file;
}

foreach my $i (0 .. $#ARGV) {
    if ($ARGV[$i] eq '-h') {
        print "Usage: $0\n";
        printf "%-20s%s\n", "-p", "Prompt for parameters";
        printf "%-20s%s\n", "-g", "Generate dtrace output";
        printf "%-20s%s\n", "-s", "Continously generate output until Ctrl^C";
        printf "%-20s%s\n", "-<n>", "Select dtrace type";
    } elsif ($ARGV[$i] eq '-g') {
        dtrace_generate;
    } elsif ($ARGV[$i] eq '-s') {
        while (1) {
            dtrace_generate;
        }
    } elsif ($ARGV[$i] eq '-p') {
        dtrace_ask_params;
    } else {
        $sampling_type = substr $ARGV[$i], 1;
        print "Select: ".$sampling_type."\n";
    }
}
