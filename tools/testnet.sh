#!/bin/sh
# SPDX-FileCopyrightText: 2026 bitzuka <bitzuka.koutnet@gmail.com>
# SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
# Sets up a second network namespace so two clients can find each other on
# one machine. Peers are told apart by address, so two instances sharing a
# host would drop each other's packets as their own.
set -e
ip netns del kout2 2>/dev/null || true
ip link del veth-h 2>/dev/null || true
ip netns add kout2
ip link add veth-h type veth peer name veth-g
ip link set veth-g netns kout2
ip addr add 10.99.0.1/24 dev veth-h
ip link set veth-h up
ip netns exec kout2 ip addr add 10.99.0.2/24 dev veth-g
ip netns exec kout2 ip link set veth-g up
ip netns exec kout2 ip link set lo up
echo "kout2 is up at 10.99.0.2, this host is 10.99.0.1 on veth-h"
