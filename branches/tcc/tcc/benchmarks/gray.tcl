proc gray-code {n} {
	expr {$n ^ ($n >> 1)}
}

if {$argc != 1} {
    puts "Usage: $argv0 n"
} else {
    set n [lindex $argv 0]

    for {set i 0} {$i < $n} {incr i} {
	gray-code $i
    }
}
