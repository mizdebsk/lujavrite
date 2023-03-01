#!/bin/sh
# Copyright (c) 2023 Red Hat, Inc.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

set -eux

JAVA_HOME=${JAVA_HOME:-/usr/lib/jvm/java}
CC=${CC:-cc -std=c99}
CFLAGS=${CFLAGS:--g -Wall -Wextra}
LDFLAGS=${LDFLAGS:-}

${CC} \
    -shared \
    -fPIC \
    -I${JAVA_HOME}/include \
    -I${JAVA_HOME}/include/linux \
    ${CFLAGS} \
    ${LDFLAGS} \
    -o lujavrite.so \
    lujavrite.c
