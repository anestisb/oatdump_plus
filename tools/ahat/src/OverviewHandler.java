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

import com.android.ahat.heapdump.AhatHeap;
import com.android.ahat.heapdump.AhatSnapshot;
import com.android.ahat.heapdump.Diffable;
import java.io.File;
import java.io.IOException;
import java.util.Collections;
import java.util.List;

class OverviewHandler implements AhatHandler {

  private static final String OVERVIEW_ID = "overview";

  private AhatSnapshot mSnapshot;
  private File mHprof;
  private File mBaseHprof;

  public OverviewHandler(AhatSnapshot snapshot, File hprof, File basehprof) {
    mSnapshot = snapshot;
    mHprof = hprof;
    mBaseHprof = basehprof;
  }

  @Override
  public void handle(Doc doc, Query query) throws IOException {
    doc.title("Overview");

    doc.section("General Information");
    doc.descriptions();
    doc.description(
        DocString.text("ahat version"),
        DocString.format("ahat-%s", OverviewHandler.class.getPackage().getImplementationVersion()));
    doc.description(DocString.text("hprof file"), DocString.text(mHprof.toString()));
    if (mBaseHprof != null) {
      doc.description(DocString.text("baseline hprof file"), DocString.text(mBaseHprof.toString()));
    }
    doc.end();

    doc.section("Heap Sizes");
    printHeapSizes(doc, query);

    doc.big(Menu.getMenu());
  }

  private static class TableElem implements Diffable<TableElem> {
    @Override public TableElem getBaseline() {
      return this;
    }

    @Override public boolean isPlaceHolder() {
      return false;
    }
  }

  private void printHeapSizes(Doc doc, Query query) {
    List<TableElem> dummy = Collections.singletonList(new TableElem());

    HeapTable.TableConfig<TableElem> table = new HeapTable.TableConfig<TableElem>() {
      public String getHeapsDescription() {
        return "Bytes Retained by Heap";
      }

      public long getSize(TableElem element, AhatHeap heap) {
        return heap.getSize();
      }

      public List<HeapTable.ValueConfig<TableElem>> getValueConfigs() {
        return Collections.emptyList();
      }
    };
    HeapTable.render(doc, query, OVERVIEW_ID, table, mSnapshot, dummy);
  }
}

