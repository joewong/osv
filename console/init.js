/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

//
// init.js
//
// initialization script for the cli environment
//

// setup networking

$("ifconfig eth0 192.168.122.89 netmask 255.255.255.0 up");
$("route add default gw 192.168.122.1");
$("java com.cloudius.cli.util.TelnetCLI &")
