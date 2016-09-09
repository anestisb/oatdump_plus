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
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;

/**
 * Class for rendering a table that includes sizes of some kind for each heap.
 */
class HeapTable {
  /**
   * Configuration for a value column of a heap table.
   */
  public interface ValueConfig<T> {
    String getDescription();
    DocString render(T element);
  }

  /**
   * Configuration for the HeapTable.
   */
  public interface TableConfig<T> {
    String getHeapsDescription();
    long getSize(T element, AhatHeap heap);
    List<ValueConfig<T>> getValueConfigs();
  }

  /**
   * Render the table to the given document.
   * @param query - The page query.
   * @param id - A unique identifier for the table on the page.
   */
  public static <T> void render(Doc doc, Query query, String id,
      TableConfig<T> config, AhatSnapshot snapshot, List<T> elements) {
    // Only show the heaps that have non-zero entries.
    List<AhatHeap> heaps = new ArrayList<AhatHeap>();
    for (AhatHeap heap : snapshot.getHeaps()) {
      if (hasNonZeroEntry(heap, config, elements)) {
        heaps.add(heap);
      }
    }

    List<ValueConfig<T>> values = config.getValueConfigs();

    // Print the heap and values descriptions.
    boolean showTotal = heaps.size() > 1;
    List<Column> subcols = new ArrayList<Column>();
    for (AhatHeap heap : heaps) {
      subcols.add(new Column(heap.getName(), Column.Align.RIGHT));
    }
    if (showTotal) {
      subcols.add(new Column("Total", Column.Align.RIGHT));
    }
    List<Column> cols = new ArrayList<Column>();
    for (ValueConfig value : values) {
      cols.add(new Column(value.getDescription()));
    }
    doc.table(DocString.text(config.getHeapsDescription()), subcols, cols);

    // Print the entries up to the selected limit.
    SubsetSelector<T> selector = new SubsetSelector(query, id, elements);
    ArrayList<DocString> vals = new ArrayList<DocString>();
    for (T elem : selector.selected()) {
      vals.clear();
      long total = 0;
      for (AhatHeap heap : heaps) {
        long size = config.getSize(elem, heap);
        total += size;
        vals.add(size == 0 ? DocString.text("") : DocString.format("%,14d", size));
      }
      if (showTotal) {
        vals.add(total == 0 ? DocString.text("") : DocString.format("%,14d", total));
      }

      for (ValueConfig<T> value : values) {
        vals.add(value.render(elem));
      }
      doc.row(vals.toArray(new DocString[0]));
    }

    // Print a summary of the remaining entries if there are any.
    List<T> remaining = selector.remaining();
    if (!remaining.isEmpty()) {
      Map<AhatHeap, Long> summary = new HashMap<AhatHeap, Long>();
      for (AhatHeap heap : heaps) {
        summary.put(heap, 0L);
      }

      for (T elem : remaining) {
        for (AhatHeap heap : heaps) {
          summary.put(heap, summary.get(heap) + config.getSize(elem, heap));
        }
      }

      vals.clear();
      long total = 0;
      for (AhatHeap heap : heaps) {
        long size = summary.get(heap);
        total += size;
        vals.add(DocString.format("%,14d", size));
      }
      if (showTotal) {
        vals.add(DocString.format("%,14d", total));
      }

      for (ValueConfig<T> value : values) {
        vals.add(DocString.text("..."));
      }
      doc.row(vals.toArray(new DocString[0]));
    }
    doc.end();
    selector.render(doc);
  }

  // Returns true if the given heap has a non-zero size entry.
  public static <T> boolean hasNonZeroEntry(AhatHeap heap,
      TableConfig<T> config, List<T> elements) {
    if (heap.getSize() > 0) {
      for (T element : elements) {
        if (config.getSize(element, heap) > 0) {
          return true;
        }
      }
    }
    return false;
  }
}

