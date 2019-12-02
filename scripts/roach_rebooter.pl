#!/usr/bin/perl

$quads = 6;
$roaches = 8;
$wait = 1;

for($a = 0; $a < $quads; $a++)
{
    for($b = 1; $b < ($roaches+1); $b++)
    {
        print "killdaemon roach2-$a$b rebooter URG\n";
        `killdaemon roach2-$a$b rebooter URG`;
        sleep($wait);
    }
}