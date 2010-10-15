#!/bin/bash

case $1 in
  force-reload)
  /etc/init.d/dnsmasq force-reload
  ;;
  *)
  echo "usage $0: [force-reload]"
  exit 1
  ;;
esac
