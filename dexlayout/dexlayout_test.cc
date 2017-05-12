/*
 * Copyright (C) 2016 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <string>
#include <vector>
#include <sstream>

#include <sys/types.h>
#include <unistd.h>

#include "base/unix_file/fd_file.h"
#include "common_runtime_test.h"
#include "exec_utils.h"
#include "utils.h"

namespace art {

static const char kDexFileLayoutInputDex[] =
    "ZGV4CjAzNQD1KW3+B8NAB0f2A/ZVIBJ0aHrGIqcpVTAUAgAAcAAAAHhWNBIAAAAAAAAAAIwBAAAH"
    "AAAAcAAAAAQAAACMAAAAAQAAAJwAAAAAAAAAAAAAAAMAAACoAAAAAgAAAMAAAAAUAQAAAAEAADAB"
    "AAA4AQAAQAEAAEgBAABNAQAAUgEAAGYBAAADAAAABAAAAAUAAAAGAAAABgAAAAMAAAAAAAAAAAAA"
    "AAAAAAABAAAAAAAAAAIAAAAAAAAAAAAAAAAAAAACAAAAAAAAAAEAAAAAAAAAdQEAAAAAAAABAAAA"
    "AAAAAAIAAAAAAAAAAgAAAAAAAAB/AQAAAAAAAAEAAQABAAAAaQEAAAQAAABwEAIAAAAOAAEAAQAB"
    "AAAAbwEAAAQAAABwEAIAAAAOAAY8aW5pdD4ABkEuamF2YQAGQi5qYXZhAANMQTsAA0xCOwASTGph"
    "dmEvbGFuZy9PYmplY3Q7AAFWAAQABw48AAQABw48AAAAAQAAgIAEgAIAAAEAAYCABJgCAAAACwAA"
    "AAAAAAABAAAAAAAAAAEAAAAHAAAAcAAAAAIAAAAEAAAAjAAAAAMAAAABAAAAnAAAAAUAAAADAAAA"
    "qAAAAAYAAAACAAAAwAAAAAEgAAACAAAAAAEAAAIgAAAHAAAAMAEAAAMgAAACAAAAaQEAAAAgAAAC"
    "AAAAdQEAAAAQAAABAAAAjAEAAA==";

static const char kDexFileLayoutInputProfile[] =
    "cHJvADAwNQABCwABAAAAAAD1KW3+Y2xhc3Nlcy5kZXgBAA==";

// Dex file with catch handler unreferenced by try blocks.
// Constructed by building a dex file with try/catch blocks and hex editing.
static const char kUnreferencedCatchHandlerInputDex[] =
    "ZGV4CjAzNQD+exd52Y0f9nY5x5GmInXq5nXrO6Kl2RV4AwAAcAAAAHhWNBIAAAAAAAAAANgCAAAS"
    "AAAAcAAAAAgAAAC4AAAAAwAAANgAAAABAAAA/AAAAAQAAAAEAQAAAQAAACQBAAA0AgAARAEAANYB"
    "AADeAQAA5gEAAO4BAAAAAgAADwIAACYCAAA9AgAAUQIAAGUCAAB5AgAAfwIAAIUCAACIAgAAjAIA"
    "AKECAACnAgAArAIAAAQAAAAFAAAABgAAAAcAAAAIAAAACQAAAAwAAAAOAAAADAAAAAYAAAAAAAAA"
    "DQAAAAYAAADIAQAADQAAAAYAAADQAQAABQABABAAAAAAAAAAAAAAAAAAAgAPAAAAAQABABEAAAAD"
    "AAAAAAAAAAAAAAABAAAAAwAAAAAAAAADAAAAAAAAAMgCAAAAAAAAAQABAAEAAAC1AgAABAAAAHAQ"
    "AwAAAA4AAwABAAIAAgC6AgAAIQAAAGIAAAAaAQoAbiACABAAYgAAABoBCwBuIAIAEAAOAA0AYgAA"
    "ABoBAQBuIAIAEAAo8A0AYgAAABoBAgBuIAIAEAAo7gAAAAAAAAcAAQAHAAAABwABAAIBAg8BAhgA"
    "AQAAAAQAAAABAAAABwAGPGluaXQ+AAZDYXRjaDEABkNhdGNoMgAQSGFuZGxlclRlc3QuamF2YQAN"
    "TEhhbmRsZXJUZXN0OwAVTGphdmEvaW8vUHJpbnRTdHJlYW07ABVMamF2YS9sYW5nL0V4Y2VwdGlv"
    "bjsAEkxqYXZhL2xhbmcvT2JqZWN0OwASTGphdmEvbGFuZy9TdHJpbmc7ABJMamF2YS9sYW5nL1N5"
    "c3RlbTsABFRyeTEABFRyeTIAAVYAAlZMABNbTGphdmEvbGFuZy9TdHJpbmc7AARtYWluAANvdXQA"
    "B3ByaW50bG4AAQAHDgAEAQAHDn17AncdHoseAAAAAgAAgYAExAIBCdwCAAANAAAAAAAAAAEAAAAA"
    "AAAAAQAAABIAAABwAAAAAgAAAAgAAAC4AAAAAwAAAAMAAADYAAAABAAAAAEAAAD8AAAABQAAAAQA"
    "AAAEAQAABgAAAAEAAAAkAQAAASAAAAIAAABEAQAAARAAAAIAAADIAQAAAiAAABIAAADWAQAAAyAA"
    "AAIAAAC1AgAAACAAAAEAAADIAgAAABAAAAEAAADYAgAA";

// Dex file with 0-size (catch all only) catch handler unreferenced by try blocks.
// Constructed by building a dex file with try/catch blocks and hex editing.
static const char kUnreferenced0SizeCatchHandlerInputDex[] =
    "ZGV4CjAzNQCEbEEvMstSNpQpjPdfMEfUBS48cis2QRJoAwAAcAAAAHhWNBIAAAAAAAAAAMgCAAAR"
    "AAAAcAAAAAcAAAC0AAAAAwAAANAAAAABAAAA9AAAAAQAAAD8AAAAAQAAABwBAAAsAgAAPAEAAOoB"
    "AADyAQAABAIAABMCAAAqAgAAPgIAAFICAABmAgAAaQIAAG0CAACCAgAAhgIAAIoCAACQAgAAlQIA"
    "AJ4CAACiAgAAAgAAAAMAAAAEAAAABQAAAAYAAAAHAAAACQAAAAcAAAAFAAAAAAAAAAgAAAAFAAAA"
    "3AEAAAgAAAAFAAAA5AEAAAQAAQANAAAAAAAAAAAAAAAAAAIADAAAAAEAAQAOAAAAAgAAAAAAAAAA"
    "AAAAAQAAAAIAAAAAAAAAAQAAAAAAAAC5AgAAAAAAAAEAAQABAAAApgIAAAQAAABwEAMAAAAOAAQA"
    "AQACAAIAqwIAAC8AAABiAAAAGgEPAG4gAgAQAGIAAAAaAQoAbiACABAAYgAAABoBEABuIAIAEABi"
    "AAAAGgELAG4gAgAQAA4ADQBiAQAAGgIKAG4gAgAhACcADQBiAQAAGgILAG4gAgAhACcAAAAAAAAA"
    "BwABAA4AAAAHAAEAAgAdACYAAAABAAAAAwAAAAEAAAAGAAY8aW5pdD4AEEhhbmRsZXJUZXN0Lmph"
    "dmEADUxIYW5kbGVyVGVzdDsAFUxqYXZhL2lvL1ByaW50U3RyZWFtOwASTGphdmEvbGFuZy9PYmpl"
    "Y3Q7ABJMamF2YS9sYW5nL1N0cmluZzsAEkxqYXZhL2xhbmcvU3lzdGVtOwABVgACVkwAE1tMamF2"
    "YS9sYW5nL1N0cmluZzsAAmYxAAJmMgAEbWFpbgADb3V0AAdwcmludGxuAAJ0MQACdDIAAQAHDgAE"
    "AQAHDnl7eXkCeB2bAAAAAgAAgYAEvAIBCdQCAA0AAAAAAAAAAQAAAAAAAAABAAAAEQAAAHAAAAAC"
    "AAAABwAAALQAAAADAAAAAwAAANAAAAAEAAAAAQAAAPQAAAAFAAAABAAAAPwAAAAGAAAAAQAAABwB"
    "AAABIAAAAgAAADwBAAABEAAAAgAAANwBAAACIAAAEQAAAOoBAAADIAAAAgAAAKYCAAAAIAAAAQAA"
    "ALkCAAAAEAAAAQAAAMgCAAA=";

// Dex file with an unreferenced catch handler at end of code item.
// Constructed by building a dex file with try/catch blocks and hex editing.
static const char kUnreferencedEndingCatchHandlerInputDex[] =
    "ZGV4CjAzNQCEflufI6xGTDDRmLpbfYi6ujPrDLIwvYcEBAAAcAAAAHhWNBIAAAAAAAAAAGQDAAAT"
    "AAAAcAAAAAgAAAC8AAAAAwAAANwAAAABAAAAAAEAAAUAAAAIAQAAAQAAADABAAC0AgAAUAEAAE4C"
    "AABWAgAAXgIAAGYCAAB4AgAAhwIAAJ4CAAC1AgAAyQIAAN0CAADxAgAA9wIAAP0CAAAAAwAABAMA"
    "ABkDAAAcAwAAIgMAACcDAAAEAAAABQAAAAYAAAAHAAAACAAAAAkAAAAMAAAADgAAAAwAAAAGAAAA"
    "AAAAAA0AAAAGAAAAQAIAAA0AAAAGAAAASAIAAAUAAQARAAAAAAAAAAAAAAAAAAAADwAAAAAAAgAQ"
    "AAAAAQABABIAAAADAAAAAAAAAAAAAAABAAAAAwAAAAAAAAADAAAAAAAAAFADAAAAAAAAAQABAAEA"
    "AAAwAwAABAAAAHAQBAAAAA4AAgAAAAIAAgA1AwAAIQAAAGIAAAAaAQoAbiADABAAYgAAABoBCwBu"
    "IAMAEAAOAA0AYgAAABoBAQBuIAMAEAAo8A0AYgAAABoBAgBuIAMAEAAo7gAAAAAAAAcAAQAHAAAA"
    "BwABAAIBAg8BAhgAAwABAAIAAgBCAwAAIQAAAGIAAAAaAQoAbiADABAAYgAAABoBCwBuIAMAEAAO"
    "AA0AYgAAABoBAQBuIAMAEAAo8A0AYgAAABoBAgBuIAMAEAAo7gAAAAAAAAcAAQAHAAAABwABAAIB"
    "Ag8BAhgAAQAAAAQAAAABAAAABwAGPGluaXQ+AAZDYXRjaDEABkNhdGNoMgAQSGFuZGxlclRlc3Qu"
    "amF2YQANTEhhbmRsZXJUZXN0OwAVTGphdmEvaW8vUHJpbnRTdHJlYW07ABVMamF2YS9sYW5nL0V4"
    "Y2VwdGlvbjsAEkxqYXZhL2xhbmcvT2JqZWN0OwASTGphdmEvbGFuZy9TdHJpbmc7ABJMamF2YS9s"
    "YW5nL1N5c3RlbTsABFRyeTEABFRyeTIAAVYAAlZMABNbTGphdmEvbGFuZy9TdHJpbmc7AAFhAARt"
    "YWluAANvdXQAB3ByaW50bG4AAQAHDgAEAAcOfHsCeB0eih4AEQEABw59ewJ3HR6LHgAAAAMAAIGA"
    "BNACAQnoAgEJ1AMAAA0AAAAAAAAAAQAAAAAAAAABAAAAEwAAAHAAAAACAAAACAAAALwAAAADAAAA"
    "AwAAANwAAAAEAAAAAQAAAAABAAAFAAAABQAAAAgBAAAGAAAAAQAAADABAAABIAAAAwAAAFABAAAB"
    "EAAAAgAAAEACAAACIAAAEwAAAE4CAAADIAAAAwAAADADAAAAIAAAAQAAAFADAAAAEAAAAQAAAGQD"
    "AAA=";

// Dex file with multiple code items that have the same debug_info_off_. Constructed by a modified
// dexlayout on XandY.
static const char kDexFileDuplicateOffset[] =
    "ZGV4CjAzNwAQfXfPCB8qCxo7MqdFhmHZQwCv8+udHD8MBAAAcAAAAHhWNBIAAAAAAAAAAFQDAAAT"
    "AAAAcAAAAAgAAAC8AAAAAQAAANwAAAABAAAA6AAAAAUAAADwAAAAAwAAABgBAACUAgAAeAEAABQC"
    "AAAeAgAAJgIAACsCAAAyAgAANwIAAFsCAAB7AgAAngIAALICAAC1AgAAvQIAAMUCAADIAgAA1QIA"
    "AOkCAADvAgAA9QIAAPwCAAACAAAAAwAAAAQAAAAFAAAABgAAAAcAAAAIAAAACQAAAAkAAAAHAAAA"
    "AAAAAAIAAQASAAAAAAAAAAEAAAABAAAAAQAAAAIAAAAAAAAAAgAAAAEAAAAGAAAAAQAAAAAAAAAA"
    "AAAABgAAAAAAAAAKAAAAAAAAACsDAAAAAAAAAQAAAAAAAAAGAAAAAAAAAAsAAAD0AQAANQMAAAAA"
    "AAACAAAAAAAAAAAAAAAAAAAACwAAAAQCAAA/AwAAAAAAAAIAAAAUAwAAGgMAAAEAAAAjAwAAAQAB"
    "AAEAAAAFAAAABAAAAHAQBAAAAA4AAQABAAEAAAAFAAAABAAAAHAQBAAAAA4AAQAAAAEAAAAFAAAA"
    "CAAAACIAAQBwEAEAAABpAAAADgABAAEAAQAAAAUAAAAEAAAAcBAAAAAADgB4AQAAAAAAAAAAAAAA"
    "AAAAhAEAAAAAAAAAAAAAAAAAAAg8Y2xpbml0PgAGPGluaXQ+AANMWDsABUxZJFo7AANMWTsAIkxk"
    "YWx2aWsvYW5ub3RhdGlvbi9FbmNsb3NpbmdDbGFzczsAHkxkYWx2aWsvYW5ub3RhdGlvbi9Jbm5l"
    "ckNsYXNzOwAhTGRhbHZpay9hbm5vdGF0aW9uL01lbWJlckNsYXNzZXM7ABJMamF2YS9sYW5nL09i"
    "amVjdDsAAVYABlguamF2YQAGWS5qYXZhAAFaAAthY2Nlc3NGbGFncwASZW1pdHRlcjogamFjay00"
    "LjI1AARuYW1lAAR0aGlzAAV2YWx1ZQABegARAAcOABMABw4AEgAHDnYAEQAHDgACAwERGAICBAIN"
    "BAgPFwwCBQERHAEYAQAAAQAAgIAEjAMAAAEAAYCABKQDAQACAAAIAoiABLwDAYCABNwDAAAADwAA"
    "AAAAAAABAAAAAAAAAAEAAAATAAAAcAAAAAIAAAAIAAAAvAAAAAMAAAABAAAA3AAAAAQAAAABAAAA"
    "6AAAAAUAAAAFAAAA8AAAAAYAAAADAAAAGAEAAAMQAAACAAAAeAEAAAEgAAAEAAAAjAEAAAYgAAAC"
    "AAAA9AEAAAIgAAATAAAAFAIAAAMgAAAEAAAA/wIAAAQgAAADAAAAFAMAAAAgAAADAAAAKwMAAAAQ"
    "AAABAAAAVAMAAA==";

// Dex file with null value for annotations_off in the annotation_set_ref_list.
// Constructed by building a dex file with annotations and hex editing.
static const char kNullSetRefListElementInputDex[] =
    "ZGV4CjAzNQB1iA+7ZwgkF+7E6ZesYFc2lRAR3qnRAanwAwAAcAAAAHhWNBIAAAAAAAAAACADAAAS"
    "AAAAcAAAAAgAAAC4AAAAAwAAANgAAAABAAAA/AAAAAQAAAAEAQAAAgAAACQBAACMAgAAZAEAAOgB"
    "AADwAQAAAAIAAAMCAAAQAgAAIAIAADQCAABIAgAAawIAAI0CAAC1AgAAyAIAANECAADUAgAA2QIA"
    "ANwCAADjAgAA6QIAAAMAAAAEAAAABQAAAAYAAAAHAAAACAAAAAkAAAAMAAAAAgAAAAMAAAAAAAAA"
    "DAAAAAcAAAAAAAAADQAAAAcAAADgAQAABgAGAAsAAAAAAAEAAAAAAAAAAgAOAAAAAQAAABAAAAAC"
    "AAEAAAAAAAAAAAAAAAAAAgAAAAAAAAABAAAAsAEAAAgDAAAAAAAAAQAAAAEmAAACAAAA2AEAAAoA"
    "AADIAQAAFgMAAAAAAAACAAAAAAAAAHwBAAABAAAA/AIAAAAAAAABAAAAAgMAAAEAAQABAAAA8AIA"
    "AAQAAABwEAMAAAAOAAIAAgAAAAAA9QIAAAEAAAAOAAAAAAAAAAAAAAAAAAAAAQAAAAEAAABkAQAA"
    "cAEAAAAAAAAAAAAAAAAAAAEAAAAEAAAAAgAAAAMAAwAGPGluaXQ+AA5Bbm5vQ2xhc3MuamF2YQAB"
    "TAALTEFubm9DbGFzczsADkxNeUFubm90YXRpb247ABJMamF2YS9sYW5nL09iamVjdDsAEkxqYXZh"
    "L2xhbmcvU3RyaW5nOwAhTGphdmEvbGFuZy9hbm5vdGF0aW9uL0Fubm90YXRpb247ACBMamF2YS9s"
    "YW5nL2Fubm90YXRpb24vUmV0ZW50aW9uOwAmTGphdmEvbGFuZy9hbm5vdGF0aW9uL1JldGVudGlv"
    "blBvbGljeTsAEU15QW5ub3RhdGlvbi5qYXZhAAdSVU5USU1FAAFWAANWTEwAAWEABWFOYW1lAARu"
    "YW1lAAV2YWx1ZQABAAcOAAICAAAHDgABBQERGwABAQEQFw8AAAIAAICABIQDAQmcAwAAAAECgQgA"
    "AAARAAAAAAAAAAEAAAAAAAAAAQAAABIAAABwAAAAAgAAAAgAAAC4AAAAAwAAAAMAAADYAAAABAAA"
    "AAEAAAD8AAAABQAAAAQAAAAEAQAABgAAAAIAAAAkAQAAAhAAAAEAAABkAQAAAxAAAAMAAABwAQAA"
    "ASAAAAIAAACEAQAABiAAAAIAAACwAQAAARAAAAIAAADYAQAAAiAAABIAAADoAQAAAyAAAAIAAADw"
    "AgAABCAAAAIAAAD8AgAAACAAAAIAAAAIAwAAABAAAAEAAAAgAwAA";

// Dex file with shared empty class data item for multiple class defs.
// Constructing by building a dex file with multiple classes and hex editing.
static const char kMultiClassDataInputDex[] =
    "ZGV4CjAzNQALJgF9TtnLq748xVe/+wyxETrT9lTEiW6YAQAAcAAAAHhWNBIAAAAAAAAAADQBAAAI"
    "AAAAcAAAAAQAAACQAAAAAAAAAAAAAAACAAAAoAAAAAAAAAAAAAAAAgAAALAAAACoAAAA8AAAAPAA"
    "AAD4AAAAAAEAAAMBAAAIAQAADQEAACEBAAAkAQAAAgAAAAMAAAAEAAAABQAAAAEAAAAGAAAAAgAA"
    "AAcAAAABAAAAAQYAAAMAAAAAAAAAAAAAAAAAAAAnAQAAAAAAAAIAAAABBgAAAwAAAAAAAAABAAAA"
    "AAAAACcBAAAAAAAABkEuamF2YQAGQi5qYXZhAAFJAANMQTsAA0xCOwASTGphdmEvbGFuZy9PYmpl"
    "Y3Q7AAFhAAFiAAAAAAABAAAAARkAAAAIAAAAAAAAAAEAAAAAAAAAAQAAAAgAAABwAAAAAgAAAAQA"
    "AACQAAAABAAAAAIAAACgAAAABgAAAAIAAACwAAAAAiAAAAgAAADwAAAAACAAAAIAAAAnAQAAABAA"
    "AAEAAAA0AQAA";

// Dex file with code info followed by non 4-byte aligned section.
// Constructed a dex file with code info followed by string data and hex edited.
static const char kUnalignedCodeInfoInputDex[] =
    "ZGV4CjAzNQDXJzXNb4iWn2SLhmLydW/8h1K9moERIw7UAQAAcAAAAHhWNBIAAAAAAAAAAEwBAAAG"
    "AAAAcAAAAAMAAACIAAAAAQAAAJQAAAAAAAAAAAAAAAMAAACgAAAAAQAAALgAAAD8AAAA2AAAAAIB"
    "AAAKAQAAEgEAABcBAAArAQAALgEAAAIAAAADAAAABAAAAAQAAAACAAAAAAAAAAAAAAAAAAAAAAAA"
    "AAUAAAABAAAAAAAAAAAAAAABAAAAAQAAAAAAAAABAAAAAAAAADsBAAAAAAAAAQABAAEAAAAxAQAA"
    "BAAAAHAQAgAAAA4AAQABAAAAAAA2AQAAAQAAAA4ABjxpbml0PgAGQS5qYXZhAANMQTsAEkxqYXZh"
    "L2xhbmcvT2JqZWN0OwABVgABYQABAAcOAAMABw4AAAABAQCBgATYAQEB8AEAAAALAAAAAAAAAAEA"
    "AAAAAAAAAQAAAAYAAABwAAAAAgAAAAMAAACIAAAAAwAAAAEAAACUAAAABQAAAAMAAACgAAAABgAA"
    "AAEAAAC4AAAAASAAAAIAAADYAAAAAiAAAAYAAAACAQAAAyAAAAIAAAAxAQAAACAAAAEAAAA7AQAA"
    "ABAAAAEAAABMAQAA";

// Dex file with class data section preceding code items.
// Constructed by passing dex file through dexmerger tool and hex editing.
static const char kClassDataBeforeCodeInputDex[] =
    "ZGV4CjAzNQCZKmCu3XXn4zvxCh5VH0gZNNobEAcsc49EAgAAcAAAAHhWNBIAAAAAAAAAAAQBAAAJ"
    "AAAAcAAAAAQAAACUAAAAAgAAAKQAAAAAAAAAAAAAAAUAAAC8AAAAAQAAAOQAAABAAQAABAEAAPgB"
    "AAAAAgAACAIAAAsCAAAQAgAAJAIAACcCAAAqAgAALQIAAAIAAAADAAAABAAAAAUAAAACAAAAAAAA"
    "AAAAAAAFAAAAAwAAAAAAAAABAAEAAAAAAAEAAAAGAAAAAQAAAAcAAAABAAAACAAAAAIAAQAAAAAA"
    "AQAAAAEAAAACAAAAAAAAAAEAAAAAAAAAjAEAAAAAAAALAAAAAAAAAAEAAAAAAAAAAQAAAAkAAABw"
    "AAAAAgAAAAQAAACUAAAAAwAAAAIAAACkAAAABQAAAAUAAAC8AAAABgAAAAEAAADkAAAAABAAAAEA"
    "AAAEAQAAACAAAAEAAACMAQAAASAAAAQAAACkAQAAAiAAAAkAAAD4AQAAAyAAAAQAAAAwAgAAAAAB"
    "AwCBgASkAwEBvAMBAdADAQHkAwAAAQABAAEAAAAwAgAABAAAAHAQBAAAAA4AAgABAAAAAAA1AgAA"
    "AgAAABIQDwACAAEAAAAAADoCAAACAAAAEiAPAAIAAQAAAAAAPwIAAAIAAAASMA8ABjxpbml0PgAG"
    "QS5qYXZhAAFJAANMQTsAEkxqYXZhL2xhbmcvT2JqZWN0OwABVgABYQABYgABYwABAAcOAAMABw4A"
    "BgAHDgAJAAcOAA==";

// Dex file with local info containing a null type descriptor.
// Constructed a dex file with debug info sequence containing DBG_RESTART_LOCAL without any
// DBG_START_LOCAL to give it a declared type.
static const char kUnknownTypeDebugInfoInputDex[] =
    "ZGV4CjAzNQBtKqZfzjHLNSNwW2A6Bz9FuCEX0sL+FF38AQAAcAAAAHhWNBIAAAAAAAAAAHQBAAAI"
    "AAAAcAAAAAQAAACQAAAAAgAAAKAAAAAAAAAAAAAAAAMAAAC4AAAAAQAAANAAAAAMAQAA8AAAABwB"
    "AAAkAQAALAEAAC8BAAA0AQAASAEAAEsBAABOAQAAAgAAAAMAAAAEAAAABQAAAAIAAAAAAAAAAAAA"
    "AAUAAAADAAAAAAAAAAEAAQAAAAAAAQAAAAYAAAACAAEAAAAAAAEAAAABAAAAAgAAAAAAAAABAAAA"
    "AAAAAGMBAAAAAAAAAQABAAEAAABUAQAABAAAAHAQAgAAAA4AAgABAAAAAABZAQAAAgAAABIQDwAG"
    "PGluaXQ+AAZBLmphdmEAAUkAA0xBOwASTGphdmEvbGFuZy9PYmplY3Q7AAFWAAFhAAR0aGlzAAEA"
    "Bw4AAwAHDh4GAAYAAAAAAQEAgYAE8AEBAYgCAAAACwAAAAAAAAABAAAAAAAAAAEAAAAIAAAAcAAA"
    "AAIAAAAEAAAAkAAAAAMAAAACAAAAoAAAAAUAAAADAAAAuAAAAAYAAAABAAAA0AAAAAEgAAACAAAA"
    "8AAAAAIgAAAIAAAAHAEAAAMgAAACAAAAVAEAAAAgAAABAAAAYwEAAAAQAAABAAAAdAEAAA==";

// Dex file with multiple class data items pointing to the same code item.
// Constructed by hex editing.
static const char kDuplicateCodeItemInputDex[] =
    "ZGV4CjAzNQCwKtVglQOmLWuHwldN5jkBOInC7mTMhJMAAgAAcAAAAHhWNBIAAAAAAAAAAHgBAAAH"
    "AAAAcAAAAAMAAACMAAAAAQAAAJgAAAAAAAAAAAAAAAQAAACkAAAAAQAAAMQAAAAcAQAA5AAAACQB"
    "AAAsAQAANAEAADkBAABNAQAAUAEAAFMBAAACAAAAAwAAAAQAAAAEAAAAAgAAAAAAAAAAAAAAAAAA"
    "AAAAAAAFAAAAAAAAAAYAAAABAAAAAAAAAAAAAAABAAAAAQAAAAAAAAABAAAAAAAAAGUBAAAAAAAA"
    "AQABAAEAAABWAQAABAAAAHAQAwAAAA4AAQABAAAAAABbAQAAAQAAAA4AAAABAAEAAAAAAGABAAAB"
    "AAAADgAAAAY8aW5pdD4ABkEuamF2YQADTEE7ABJMamF2YS9sYW5nL09iamVjdDsAAVYAAWEAAWIA"
    "AQAHDgADAAcOAAUABw4AAAABAgCBgATkAQEA/AEBAPwBAAsAAAAAAAAAAQAAAAAAAAABAAAABwAA"
    "AHAAAAACAAAAAwAAAIwAAAADAAAAAQAAAJgAAAAFAAAABAAAAKQAAAAGAAAAAQAAAMQAAAABIAAA"
    "AwAAAOQAAAACIAAABwAAACQBAAADIAAAAwAAAFYBAAAAIAAAAQAAAGUBAAAAEAAAAQAAAHgBAAA=";

static void WriteBase64ToFile(const char* base64, File* file) {
  // Decode base64.
  CHECK(base64 != nullptr);
  size_t length;
  std::unique_ptr<uint8_t[]> bytes(DecodeBase64(base64, &length));
  CHECK(bytes != nullptr);
  if (!file->WriteFully(bytes.get(), length)) {
    PLOG(FATAL) << "Failed to write base64 as file";
  }
}

static void WriteFileBase64(const char* base64, const char* location) {
  // Write to provided file.
  std::unique_ptr<File> file(OS::CreateEmptyFile(location));
  CHECK(file != nullptr);
  WriteBase64ToFile(base64, file.get());
  if (file->FlushCloseOrErase() != 0) {
    PLOG(FATAL) << "Could not flush and close test file.";
  }
}

class DexLayoutTest : public CommonRuntimeTest {
 protected:
  virtual void SetUp() {
    CommonRuntimeTest::SetUp();
  }

  // Runs FullPlainOutput test.
  bool FullPlainOutputExec(std::string* error_msg) {
    // TODO: dexdump2 -> dexdump ?
    ScratchFile dexdump_output;
    const std::string& dexdump_filename = dexdump_output.GetFilename();
    std::string dexdump = GetTestAndroidRoot() + "/bin/dexdump2";
    EXPECT_TRUE(OS::FileExists(dexdump.c_str())) << dexdump << " should be a valid file path";

    ScratchFile dexlayout_output;
    const std::string& dexlayout_filename = dexlayout_output.GetFilename();
    std::string dexlayout = GetTestAndroidRoot() + "/bin/dexlayout";
    EXPECT_TRUE(OS::FileExists(dexlayout.c_str())) << dexlayout << " should be a valid file path";

    for (const std::string &dex_file : GetLibCoreDexFileNames()) {
      std::vector<std::string> dexdump_exec_argv =
          { dexdump, "-d", "-f", "-h", "-l", "plain", "-o", dexdump_filename, dex_file };
      std::vector<std::string> dexlayout_exec_argv =
          { dexlayout, "-d", "-f", "-h", "-l", "plain", "-o", dexlayout_filename, dex_file };
      if (!::art::Exec(dexdump_exec_argv, error_msg)) {
        return false;
      }
      if (!::art::Exec(dexlayout_exec_argv, error_msg)) {
        return false;
      }
      std::vector<std::string> diff_exec_argv =
          { "/usr/bin/diff", dexdump_filename, dexlayout_filename };
      if (!::art::Exec(diff_exec_argv, error_msg)) {
        return false;
      }
    }
    return true;
  }

  // Runs DexFileOutput test.
  bool DexFileOutputExec(std::string* error_msg) {
    ScratchFile tmp_file;
    const std::string& tmp_name = tmp_file.GetFilename();
    size_t tmp_last_slash = tmp_name.rfind('/');
    std::string tmp_dir = tmp_name.substr(0, tmp_last_slash + 1);
    std::string dexlayout = GetTestAndroidRoot() + "/bin/dexlayout";
    EXPECT_TRUE(OS::FileExists(dexlayout.c_str())) << dexlayout << " should be a valid file path";

    for (const std::string &dex_file : GetLibCoreDexFileNames()) {
      std::vector<std::string> dexlayout_exec_argv =
          { dexlayout, "-w", tmp_dir, "-o", tmp_name, dex_file };
      if (!::art::Exec(dexlayout_exec_argv, error_msg)) {
        return false;
      }
      size_t dex_file_last_slash = dex_file.rfind("/");
      std::string dex_file_name = dex_file.substr(dex_file_last_slash + 1);
      std::vector<std::string> unzip_exec_argv =
          { "/usr/bin/unzip", dex_file, "classes.dex", "-d", tmp_dir};
      if (!::art::Exec(unzip_exec_argv, error_msg)) {
        return false;
      }
      std::vector<std::string> diff_exec_argv =
          { "/usr/bin/diff", tmp_dir + "classes.dex" , tmp_dir + dex_file_name };
      if (!::art::Exec(diff_exec_argv, error_msg)) {
        return false;
      }
      std::vector<std::string> rm_zip_exec_argv = { "/bin/rm", tmp_dir + "classes.dex" };
      if (!::art::Exec(rm_zip_exec_argv, error_msg)) {
        return false;
      }
      std::vector<std::string> rm_out_exec_argv = { "/bin/rm", tmp_dir + dex_file_name };
      if (!::art::Exec(rm_out_exec_argv, error_msg)) {
        return false;
      }
    }
    return true;
  }

  // Runs DexFileLayout test.
  bool DexFileLayoutExec(std::string* error_msg) {
    ScratchFile tmp_file;
    std::string tmp_name = tmp_file.GetFilename();
    size_t tmp_last_slash = tmp_name.rfind("/");
    std::string tmp_dir = tmp_name.substr(0, tmp_last_slash + 1);

    // Write inputs and expected outputs.
    std::string dex_file = tmp_dir + "classes.dex";
    WriteFileBase64(kDexFileLayoutInputDex, dex_file.c_str());
    std::string profile_file = tmp_dir + "primary.prof";
    WriteFileBase64(kDexFileLayoutInputProfile, profile_file.c_str());
    std::string output_dex = tmp_dir + "classes.dex.new";

    std::string dexlayout = GetTestAndroidRoot() + "/bin/dexlayout";
    EXPECT_TRUE(OS::FileExists(dexlayout.c_str())) << dexlayout << " should be a valid file path";

    std::vector<std::string> dexlayout_exec_argv =
        { dexlayout, "-v", "-w", tmp_dir, "-o", tmp_name, "-p", profile_file, dex_file };
    if (!::art::Exec(dexlayout_exec_argv, error_msg)) {
      return false;
    }

    // -v makes sure that the layout did not corrupt the dex file.

    std::vector<std::string> rm_exec_argv =
        { "/bin/rm", dex_file, profile_file, output_dex };
    if (!::art::Exec(rm_exec_argv, error_msg)) {
      return false;
    }
    return true;
  }

  // Runs UnreferencedCatchHandlerTest & Unreferenced0SizeCatchHandlerTest.
  bool UnreferencedCatchHandlerExec(std::string* error_msg, const char* filename) {
    ScratchFile tmp_file;
    std::string tmp_name = tmp_file.GetFilename();
    size_t tmp_last_slash = tmp_name.rfind("/");
    std::string tmp_dir = tmp_name.substr(0, tmp_last_slash + 1);

    // Write inputs and expected outputs.
    std::string input_dex = tmp_dir + "classes.dex";
    WriteFileBase64(filename, input_dex.c_str());
    std::string output_dex = tmp_dir + "classes.dex.new";

    std::string dexlayout = GetTestAndroidRoot() + "/bin/dexlayout";
    EXPECT_TRUE(OS::FileExists(dexlayout.c_str())) << dexlayout << " should be a valid file path";

    std::vector<std::string> dexlayout_exec_argv =
        { dexlayout, "-w", tmp_dir, "-o", "/dev/null", input_dex };
    if (!::art::Exec(dexlayout_exec_argv, error_msg)) {
      return false;
    }

    // Diff input and output. They should be the same.
    std::vector<std::string> diff_exec_argv = { "/usr/bin/diff", input_dex, output_dex };
    if (!::art::Exec(diff_exec_argv, error_msg)) {
      return false;
    }

    std::vector<std::string> rm_exec_argv = { "/bin/rm", input_dex, output_dex };
    if (!::art::Exec(rm_exec_argv, error_msg)) {
      return false;
    }
    return true;
  }

  bool DexLayoutExec(ScratchFile* dex_file,
                     const char* dex_filename,
                     ScratchFile* profile_file,
                     const char* profile_filename,
                     std::vector<std::string>& dexlayout_exec_argv) {
    WriteBase64ToFile(dex_filename, dex_file->GetFile());
    EXPECT_EQ(dex_file->GetFile()->Flush(), 0);
    if (profile_file != nullptr) {
      WriteBase64ToFile(profile_filename, profile_file->GetFile());
      EXPECT_EQ(profile_file->GetFile()->Flush(), 0);
    }
    std::string error_msg;
    const bool result = ::art::Exec(dexlayout_exec_argv, &error_msg);
    if (!result) {
      LOG(ERROR) << "Error: " << error_msg;
      return false;
    }
    return true;
  }
};


TEST_F(DexLayoutTest, FullPlainOutput) {
  // Disable test on target.
  TEST_DISABLED_FOR_TARGET();
  std::string error_msg;
  ASSERT_TRUE(FullPlainOutputExec(&error_msg)) << error_msg;
}

TEST_F(DexLayoutTest, DexFileOutput) {
  // Disable test on target.
  TEST_DISABLED_FOR_TARGET();
  std::string error_msg;
  ASSERT_TRUE(DexFileOutputExec(&error_msg)) << error_msg;
}

TEST_F(DexLayoutTest, DexFileLayout) {
  // Disable test on target.
  TEST_DISABLED_FOR_TARGET();
  std::string error_msg;
  ASSERT_TRUE(DexFileLayoutExec(&error_msg)) << error_msg;
}

TEST_F(DexLayoutTest, UnreferencedCatchHandler) {
  // Disable test on target.
  TEST_DISABLED_FOR_TARGET();
  std::string error_msg;
  ASSERT_TRUE(UnreferencedCatchHandlerExec(&error_msg,
                                           kUnreferencedCatchHandlerInputDex)) << error_msg;
}

TEST_F(DexLayoutTest, Unreferenced0SizeCatchHandler) {
  // Disable test on target.
  TEST_DISABLED_FOR_TARGET();
  std::string error_msg;
  ASSERT_TRUE(UnreferencedCatchHandlerExec(&error_msg,
                                           kUnreferenced0SizeCatchHandlerInputDex)) << error_msg;
}

TEST_F(DexLayoutTest, UnreferencedEndingCatchHandler) {
  // Disable test on target.
  TEST_DISABLED_FOR_TARGET();
  std::string error_msg;
  ASSERT_TRUE(UnreferencedCatchHandlerExec(&error_msg,
                                           kUnreferencedEndingCatchHandlerInputDex)) << error_msg;
}

TEST_F(DexLayoutTest, DuplicateOffset) {
  ScratchFile temp_dex;
  std::string dexlayout = GetTestAndroidRoot() + "/bin/dexlayout";
  EXPECT_TRUE(OS::FileExists(dexlayout.c_str())) << dexlayout << " should be a valid file path";
  std::vector<std::string> dexlayout_exec_argv =
      { dexlayout, "-a", "-i", "-o", "/dev/null", temp_dex.GetFilename() };
  ASSERT_TRUE(DexLayoutExec(&temp_dex,
                            kDexFileDuplicateOffset,
                            nullptr /* profile_file */,
                            nullptr /* profile_filename */,
                            dexlayout_exec_argv));
}

TEST_F(DexLayoutTest, NullSetRefListElement) {
  ScratchFile temp_dex;
  std::string dexlayout = GetTestAndroidRoot() + "/bin/dexlayout";
  EXPECT_TRUE(OS::FileExists(dexlayout.c_str())) << dexlayout << " should be a valid file path";
  std::vector<std::string> dexlayout_exec_argv =
      { dexlayout, "-o", "/dev/null", temp_dex.GetFilename() };
  ASSERT_TRUE(DexLayoutExec(&temp_dex,
                            kNullSetRefListElementInputDex,
                            nullptr /* profile_file */,
                            nullptr /* profile_filename */,
                            dexlayout_exec_argv));
}

TEST_F(DexLayoutTest, MultiClassData) {
  ScratchFile temp_dex;
  ScratchFile temp_profile;
  std::string dexlayout = GetTestAndroidRoot() + "/bin/dexlayout";
  EXPECT_TRUE(OS::FileExists(dexlayout.c_str())) << dexlayout << " should be a valid file path";
  std::vector<std::string> dexlayout_exec_argv =
      { dexlayout, "-p", temp_profile.GetFilename(), "-o", "/dev/null", temp_dex.GetFilename() };
  ASSERT_TRUE(DexLayoutExec(&temp_dex,
                            kMultiClassDataInputDex,
                            &temp_profile,
                            kDexFileLayoutInputProfile,
                            dexlayout_exec_argv));
}

TEST_F(DexLayoutTest, UnalignedCodeInfo) {
  ScratchFile temp_dex;
  ScratchFile temp_profile;
  std::string dexlayout = GetTestAndroidRoot() + "/bin/dexlayout";
  EXPECT_TRUE(OS::FileExists(dexlayout.c_str())) << dexlayout << " should be a valid file path";
  std::vector<std::string> dexlayout_exec_argv =
      { dexlayout, "-p", temp_profile.GetFilename(), "-o", "/dev/null", temp_dex.GetFilename() };
  ASSERT_TRUE(DexLayoutExec(&temp_dex,
                            kUnalignedCodeInfoInputDex,
                            &temp_profile,
                            kDexFileLayoutInputProfile,
                            dexlayout_exec_argv));
}

TEST_F(DexLayoutTest, ClassDataBeforeCode) {
  ScratchFile temp_dex;
  ScratchFile temp_profile;
  std::string dexlayout = GetTestAndroidRoot() + "/bin/dexlayout";
  EXPECT_TRUE(OS::FileExists(dexlayout.c_str())) << dexlayout << " should be a valid file path";
  std::vector<std::string> dexlayout_exec_argv =
      { dexlayout, "-p", temp_profile.GetFilename(), "-o", "/dev/null", temp_dex.GetFilename() };
  ASSERT_TRUE(DexLayoutExec(&temp_dex,
                            kClassDataBeforeCodeInputDex,
                            &temp_profile,
                            kDexFileLayoutInputProfile,
                            dexlayout_exec_argv));
}

TEST_F(DexLayoutTest, UnknownTypeDebugInfo) {
  ScratchFile temp_dex;
  std::string dexlayout = GetTestAndroidRoot() + "/bin/dexlayout";
  EXPECT_TRUE(OS::FileExists(dexlayout.c_str())) << dexlayout << " should be a valid file path";
  std::vector<std::string> dexlayout_exec_argv =
      { dexlayout, "-o", "/dev/null", temp_dex.GetFilename() };
  ASSERT_TRUE(DexLayoutExec(&temp_dex,
                            kUnknownTypeDebugInfoInputDex,
                            nullptr /* profile_file */,
                            nullptr /* profile_filename */,
                            dexlayout_exec_argv));
}

TEST_F(DexLayoutTest, DuplicateCodeItem) {
  ScratchFile temp_dex;
  std::string dexlayout = GetTestAndroidRoot() + "/bin/dexlayout";
  EXPECT_TRUE(OS::FileExists(dexlayout.c_str())) << dexlayout << " should be a valid file path";
  std::vector<std::string> dexlayout_exec_argv =
      { dexlayout, "-o", "/dev/null", temp_dex.GetFilename() };
  ASSERT_TRUE(DexLayoutExec(&temp_dex,
                            kDuplicateCodeItemInputDex,
                            nullptr /* profile_file */,
                            nullptr /* profile_filename */,
                            dexlayout_exec_argv));
}

}  // namespace art
