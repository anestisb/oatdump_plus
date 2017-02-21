/*
 * Copyright (C) 2015 The Android Open Source Project
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

package com.android.ahat;

import com.android.ahat.heapdump.AhatSnapshot;
import com.android.tools.perflib.heap.ProguardMap;
import com.android.tools.perflib.heap.hprof.Hprof;
import com.android.tools.perflib.heap.hprof.HprofRecord;
import com.android.tools.perflib.heap.hprof.HprofStringBuilder;
import com.android.tools.perflib.heap.io.InMemoryBuffer;
import java.io.ByteArrayOutputStream;
import java.io.IOException;
import java.util.ArrayList;
import java.util.Date;
import java.util.List;

/**
 * Class with utilities to help constructing snapshots for tests.
 */
public class SnapshotBuilder {

  // Helper function to make a snapshot with id size 4 given an
  // HprofStringBuilder and list of HprofRecords
  public static AhatSnapshot makeSnapshot(HprofStringBuilder strings, List<HprofRecord> records)
    throws IOException {
    // TODO: When perflib can handle the case where strings are referred to
    // before they are defined, just add the string records to the records
    // list.
    List<HprofRecord> actualRecords = new ArrayList<HprofRecord>();
    actualRecords.addAll(strings.getStringRecords());
    actualRecords.addAll(records);

    Hprof hprof = new Hprof("JAVA PROFILE 1.0.3", 4, new Date(), actualRecords);
    ByteArrayOutputStream os = new ByteArrayOutputStream();
    hprof.write(os);
    InMemoryBuffer buffer = new InMemoryBuffer(os.toByteArray());
    return AhatSnapshot.fromDataBuffer(buffer, new ProguardMap());
  }
}
