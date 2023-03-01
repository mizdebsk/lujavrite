LuJavRite
=========

LuJavRite is a rock-solid Lua library that allows calling Java code
from Lua code.  It does so by launching embedded Java Virtual Machine
and using JNI interface to invoke Java methods.

For now LuJavRite can only call static methods that take zero or more
String arguments and return String.  Besides handling Strings, it can
also convert between `nil` and `null` values.

LuJavRite is free software. You can redistribute and/or modify it
under the terms of Apache License Version 2.0.

LuJavRite was written by Mikolaj Izdebski.
