#!/usr/bin/perl
#
#	makeStatTbl.pl - Create Error Symbol Table
#
#	Kay-Uwe Kasemir, 1-31-97,
#	based on makeStatTbl shell script.
#
# SYNOPSIS
# perl makeStatTbl.pl hdir [...]
#
# DESCRIPTION
# This tool creates a symbol table (ERRSYMTAB) structure which contains the
# names and values of all the status codes defined in the .h files in the
# specified directory(s).  The status codes must be prefixed with "S_"
# in order to be included in this table.
# A "err.h" file must exist in each hdir which defines the module
# numbers, eg. "M_".  The table is created on standard output.
#
# This tool's primary use is for creating an error status table used
# by errPrint, and errSymFind.
#
# FILES
# errMdef.h   module number file for each h directory
#
# SEE ALSO: errnoLib(1), symLib(1)
#*/

use Cwd;

die "No args (files to parse) given"   if ($#ARGV < 0);

#	parse all lines of all files given:
while (<>)
{
	if (m'^#define[ /t]*S_')
	{
		chomp;
		push @err_sym_line, $_;
	}
}

$out_name = "errSymTbl.c";
$dir = cwd();

open OUT, ">$out_name"  or  die "Cannot open $out_name";

print OUT "/*\n";
print OUT " * status code symbol table\n";
print OUT " *\n";
print OUT " * CREATED BY makeStatTbl.pl\n";
print OUT " *       FROM $dir\n";
print OUT " *         ON " . localtime() . "\n";
print OUT " */\n";
print OUT "\n";
print OUT "#include \"errMdef.h\"\n";
print OUT "#include \"errSymTbl.h\"\n";
print OUT "\n";

$count = 0;
foreach $line ( @err_sym_line )
{
	print OUT "$line\n";
	#                    define       S_symbol          /* comment */
	if ($line =~ m'[ \t#]define[ \t]*(S_[A-Za-z0-9_]+).*\/\*(.+)\*\/')
	{
		$symbol[$count] = $1;
		$comment[$count]= $2;
		++$count;
	}
	else
	{
		#	Some status values for '0' (=OK) have no comment:
		unless ($line =~ m'[ \t#]define[ \t]*(S_[A-Za-z0-9_]+)')
		{
			die "cannot decode this line:\n$line\n";
		}
	}
}


print OUT "\n";
print OUT "LOCAL ERRSYMBOL symbols[] =\n";
print OUT "{\n";

for ($i=0; $i<$count; ++$i)
{
	printf OUT "\t{ \"%s\", (long) %s },\n",
		$comment[$i], $symbol[$i];
}

print OUT "};\n";
print OUT "\n";
print OUT "LOCAL ERRSYMTAB symTbl =\n";
print OUT "{\n";
print OUT "\tNELEMENTS(symbols),  /* current number of symbols in table */\n";
print OUT "\tsymbols,             /* ptr to symbol array */\n";
print OUT "};\n";
print OUT "\n";
print OUT "ERRSYMTAB_ID errSymTbl = &symTbl;\n";
print OUT "\n";
print OUT "/*\tEOF $out_name */\n";

