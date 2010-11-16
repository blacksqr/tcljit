proc isprime {n} {
	if {$n < 2} {
		return 0
	}
	set i 2
	set stop [expr {$n / 2}]
	while {$i <= $stop} {
		if {$n % $i == 0} {
			return 0
		}
		set i [expr {$i + 1}]
	}
	return 1
}

if {$argc != 1} {
    puts "Usage: $argv0 n"
} else {
    set n [lindex $argv 0]

    for {set i 0} {$i <= $n} {incr i} {
	isprime $i
    }
}
