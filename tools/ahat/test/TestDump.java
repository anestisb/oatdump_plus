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

import com.android.ahat.heapdump.AhatClassObj;
import com.android.ahat.heapdump.AhatInstance;
import com.android.ahat.heapdump.AhatSnapshot;
import com.android.ahat.heapdump.Diff;
import com.android.ahat.heapdump.FieldValue;
import com.android.ahat.heapdump.Value;
import com.android.tools.perflib.heap.ProguardMap;
import java.io.File;
import java.io.IOException;
import java.text.ParseException;

/**
 * The TestDump class is used to get an AhatSnapshot for the test-dump
 * program.
 */
public class TestDump {
  // It can take on the order of a second to parse and process the test-dump
  // hprof. To avoid repeating this overhead for each test case, we cache the
  // loaded instance of TestDump and reuse it when possible. In theory the
  // test cases should not be able to modify the cached snapshot in a way that
  // is visible to other test cases.
  private static TestDump mCachedTestDump = null;

  // If the test dump fails to load the first time, it will likely fail every
  // other test we try. Rather than having to wait a potentially very long
  // time for test dump loading to fail over and over again, record when it
  // fails and don't try to load it again.
  private static boolean mTestDumpFailed = false;

  private AhatSnapshot mSnapshot = null;
  private AhatSnapshot mBaseline = null;

  /**
   * Load the test-dump.hprof and test-dump-base.hprof files.
   * The location of the files are read from the system properties
   * "ahat.test.dump.hprof" and "ahat.test.dump.base.hprof", which is expected
   * to be set on the command line.
   * The location of the proguard map for both hprof files is read from the
   * system property "ahat.test.dump.map".  For example:
   *   java -Dahat.test.dump.hprof=test-dump.hprof \
   *        -Dahat.test.dump.base.hprof=test-dump-base.hprof \
   *        -Dahat.test.dump.map=proguard.map \
   *        -jar ahat-tests.jar
   *
   * An IOException is thrown if there is a failure reading the hprof files or
   * the proguard map.
   */
  private TestDump() throws IOException {
    // TODO: Make use of the baseline hprof for tests.
    String hprof = System.getProperty("ahat.test.dump.hprof");
    String hprofBase = System.getProperty("ahat.test.dump.base.hprof");

    String mapfile = System.getProperty("ahat.test.dump.map");
    ProguardMap map = new ProguardMap();
    try {
      map.readFromFile(new File(mapfile));
    } catch (ParseException e) {
      throw new IOException("Unable to load proguard map", e);
    }

    mSnapshot = AhatSnapshot.fromHprof(new File(hprof), map);
    mBaseline = AhatSnapshot.fromHprof(new File(hprofBase), map);
    Diff.snapshots(mSnapshot, mBaseline);
  }

  /**
   * Get the AhatSnapshot for the test dump program.
   */
  public AhatSnapshot getAhatSnapshot() {
    return mSnapshot;
  }

  /**
   * Get the baseline AhatSnapshot for the test dump program.
   */
  public AhatSnapshot getBaselineAhatSnapshot() {
    return mBaseline;
  }

  /**
   * Returns the value of a field in the DumpedStuff instance in the
   * snapshot for the test-dump program.
   */
  public Value getDumpedValue(String name) {
    return getDumpedValue(name, mSnapshot);
  }

  /**
   * Returns the value of a field in the DumpedStuff instance in the
   * baseline snapshot for the test-dump program.
   */
  public Value getBaselineDumpedValue(String name) {
    return getDumpedValue(name, mBaseline);
  }

  /**
   * Returns the value of a field in the DumpedStuff instance in the
   * given snapshot for the test-dump program.
   */
  private Value getDumpedValue(String name, AhatSnapshot snapshot) {
    AhatClassObj main = snapshot.findClass("Main");
    AhatInstance stuff = null;
    for (FieldValue fields : main.getStaticFieldValues()) {
      if ("stuff".equals(fields.getName())) {
        stuff = fields.getValue().asAhatInstance();
      }
    }
    return stuff.getField(name);
  }

  /**
   * Returns the value of a non-primitive field in the DumpedStuff instance in
   * the snapshot for the test-dump program.
   */
  public AhatInstance getDumpedAhatInstance(String name) {
    Value value = getDumpedValue(name);
    return value == null ? null : value.asAhatInstance();
  }

  /**
   * Returns the value of a non-primitive field in the DumpedStuff instance in
   * the baseline snapshot for the test-dump program.
   */
  public AhatInstance getBaselineDumpedAhatInstance(String name) {
    Value value = getBaselineDumpedValue(name);
    return value == null ? null : value.asAhatInstance();
  }

  /**
   * Get the test dump.
   * An IOException is thrown if there is an error reading the test dump hprof
   * file.
   * To improve performance, this returns a cached instance of the TestDump
   * when possible.
   */
  public static synchronized TestDump getTestDump() throws IOException {
    if (mTestDumpFailed) {
      throw new RuntimeException("Test dump failed before, assuming it will again");
    }

    if (mCachedTestDump == null) {
      mTestDumpFailed = true;
      mCachedTestDump = new TestDump();
      mTestDumpFailed = false;
    }
    return mCachedTestDump;
  }
}
