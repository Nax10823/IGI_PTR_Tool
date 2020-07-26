#!/bin/bash

PTRCLIENT_PATH \
	-v TARGET_IP \
	-f "DUMP_DIR_PATH/trace_$(date +%s).txt"
