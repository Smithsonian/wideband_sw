#!/usr/bin/perl


$roaches = 8;
$wait = 2;

$num_args = $#ARGV + 1;
#print "$num_args\n";
if ($num_args < 1 || $num_args > 6) {
    print "\nUsage: roachrebooter.pl at least 1 quad, and no more than 6\n";
    exit;
}

for ($argnum = 0; $argnum < $num_args; $argnum++) {
  $quad[$argnum]=$ARGV[$argnum];
#  print "$quad[$argnum]\n";
  if ($quad[$argnum] < 1 || $quad[$argnum] > 6) {
     print "Please enter a quadrant number between 1 and 6\n";
     exit;
                              } 
                                 }
#print "@quad\n";

@sortedquad = sort { $a <=> $b } @quad;

#print "@sortedquad\n";

@sortedunique = ();
       %seen   = ();

       foreach $elem ( @sortedquad )
       {
         next if $seen{ $elem }++;
         push @sortedunique, $elem;
       }

#print "@sortedunique\n";

foreach $quadval (@sortedunique)
{
$quadval=$quadval-1;
    for($b = 1; $b < ($roaches+1); $b++)
    {
#    print "$quadval$b\n";
	print "killdaemon roach2-$quadval$b rebooter URG\n";
	`killdaemon roach2-$quadval$b rebooter URG`;
	sleep($wait);
    }
}
