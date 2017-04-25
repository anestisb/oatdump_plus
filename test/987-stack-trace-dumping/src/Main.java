/*
 * Copyright (C) 2017 The Android Open Source Project
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

import java.io.File;

public class Main {
    public static void main(String[] args) throws Exception {
        if (args.length != 3) {
            throw new AssertionError("Unexpected number of args: " + args.length);
        }

        if (!"--stack-trace-dir".equals(args[1])) {
            throw new AssertionError("Unexpected argument in position 1: " + args[1]);
        }

        // Send ourselves signal 3, which forces stack traces to be written to disk.
        android.system.Os.kill(android.system.Os.getpid(), 3);

        File[] files = null;
        final String stackTraceDir = args[2];
        for (int i = 0; i < 5; ++i) {
            // Give the signal handler some time to run and dump traces - up to a maximum
            // of 5 seconds. This is a kludge, but it's hard to do this without using things
            // like inotify / WatchService and the like.
            Thread.sleep(1000);

            files = (new File(stackTraceDir)).listFiles();
            if (files != null && files.length == 1) {
                break;
            }
        }


        if (files == null) {
            throw new AssertionError("Gave up waiting for traces: " + java.util.Arrays.toString(files));
        }

        final String fileName = files[0].getName();
        if (!fileName.startsWith("anr-pid")) {
            throw new AssertionError("Unexpected prefix: " + fileName);
        }

        if (!fileName.contains(String.valueOf(android.system.Os.getpid()))) {
            throw new AssertionError("File name does not contain process PID: " + fileName);
        }
    }
}
