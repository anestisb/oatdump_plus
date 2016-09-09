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

import com.android.ahat.heapdump.AhatArrayInstance;
import com.android.ahat.heapdump.AhatClassInstance;
import com.android.ahat.heapdump.AhatClassObj;
import com.android.ahat.heapdump.AhatHeap;
import com.android.ahat.heapdump.AhatInstance;
import com.android.ahat.heapdump.AhatSnapshot;
import com.android.ahat.heapdump.FieldValue;
import com.android.ahat.heapdump.PathElement;
import com.android.ahat.heapdump.Site;
import com.android.ahat.heapdump.Value;
import java.io.IOException;
import java.util.Collection;
import java.util.Collections;
import java.util.List;


class ObjectHandler implements AhatHandler {

  private static final String ARRAY_ELEMENTS_ID = "elements";
  private static final String DOMINATOR_PATH_ID = "dompath";
  private static final String ALLOCATION_SITE_ID = "frames";
  private static final String DOMINATED_OBJECTS_ID = "dominated";
  private static final String INSTANCE_FIELDS_ID = "ifields";
  private static final String STATIC_FIELDS_ID = "sfields";
  private static final String HARD_REFS_ID = "refs";
  private static final String SOFT_REFS_ID = "srefs";

  private AhatSnapshot mSnapshot;

  public ObjectHandler(AhatSnapshot snapshot) {
    mSnapshot = snapshot;
  }

  @Override
  public void handle(Doc doc, Query query) throws IOException {
    long id = query.getLong("id", 0);
    AhatInstance inst = mSnapshot.findInstance(id);
    if (inst == null) {
      doc.println(DocString.format("No object with id %08xl", id));
      return;
    }

    doc.title("Object %08x", inst.getId());
    doc.big(Summarizer.summarize(inst));

    printAllocationSite(doc, query, inst);
    printGcRootPath(doc, query, inst);

    doc.section("Object Info");
    AhatClassObj cls = inst.getClassObj();
    doc.descriptions();
    doc.description(DocString.text("Class"), Summarizer.summarize(cls));
    doc.description(DocString.text("Size"), DocString.format("%d", inst.getSize()));
    doc.description(
        DocString.text("Retained Size"),
        DocString.format("%d", inst.getTotalRetainedSize()));
    doc.description(DocString.text("Heap"), DocString.text(inst.getHeap().getName()));

    Collection<String> rootTypes = inst.getRootTypes();
    if (rootTypes != null) {
      DocString types = new DocString();
      String comma = "";
      for (String type : rootTypes) {
        types.append(comma);
        types.append(type);
        comma = ", ";
      }
      doc.description(DocString.text("Root Types"), types);
    }

    doc.end();

    printBitmap(doc, inst);
    if (inst.isClassInstance()) {
      printClassInstanceFields(doc, query, inst.asClassInstance());
    } else if (inst.isArrayInstance()) {
      printArrayElements(doc, query, inst.asArrayInstance());
    } else if (inst.isClassObj()) {
      printClassInfo(doc, query, inst.asClassObj());
    }
    printReferences(doc, query, inst);
    printDominatedObjects(doc, query, inst);
  }

  private static void printClassInstanceFields(Doc doc, Query query, AhatClassInstance inst) {
    doc.section("Fields");
    doc.table(new Column("Type"), new Column("Name"), new Column("Value"));
    SubsetSelector<FieldValue> selector
      = new SubsetSelector(query, INSTANCE_FIELDS_ID, inst.getInstanceFields());
    for (FieldValue field : selector.selected()) {
      doc.row(
          DocString.text(field.getType()),
          DocString.text(field.getName()),
          Summarizer.summarize(field.getValue()));
    }
    doc.end();
    selector.render(doc);
  }

  private static void printArrayElements(Doc doc, Query query, AhatArrayInstance array) {
    doc.section("Array Elements");
    doc.table(new Column("Index", Column.Align.RIGHT), new Column("Value"));
    List<Value> elements = array.getValues();
    SubsetSelector<Value> selector = new SubsetSelector(query, ARRAY_ELEMENTS_ID, elements);
    int i = 0;
    for (Value elem : selector.selected()) {
      doc.row(DocString.format("%d", i), Summarizer.summarize(elem));
      i++;
    }
    doc.end();
    selector.render(doc);
  }

  private static void printClassInfo(Doc doc, Query query, AhatClassObj clsobj) {
    doc.section("Class Info");
    doc.descriptions();
    doc.description(DocString.text("Super Class"),
        Summarizer.summarize(clsobj.getSuperClassObj()));
    doc.description(DocString.text("Class Loader"),
        Summarizer.summarize(clsobj.getClassLoader()));
    doc.end();

    doc.section("Static Fields");
    doc.table(new Column("Type"), new Column("Name"), new Column("Value"));
    List<FieldValue> fields = clsobj.getStaticFieldValues();
    SubsetSelector<FieldValue> selector = new SubsetSelector(query, STATIC_FIELDS_ID, fields);
    for (FieldValue field : selector.selected()) {
      doc.row(
          DocString.text(field.getType()),
          DocString.text(field.getName()),
          Summarizer.summarize(field.getValue()));
    }
    doc.end();
    selector.render(doc);
  }

  private static void printReferences(Doc doc, Query query, AhatInstance inst) {
    doc.section("Objects with References to this Object");
    if (inst.getHardReverseReferences().isEmpty()) {
      doc.println(DocString.text("(none)"));
    } else {
      doc.table(new Column("Object"));
      List<AhatInstance> references = inst.getHardReverseReferences();
      SubsetSelector<AhatInstance> selector = new SubsetSelector(query, HARD_REFS_ID, references);
      for (AhatInstance ref : selector.selected()) {
        doc.row(Summarizer.summarize(ref));
      }
      doc.end();
      selector.render(doc);
    }

    if (!inst.getSoftReverseReferences().isEmpty()) {
      doc.section("Objects with Soft References to this Object");
      doc.table(new Column("Object"));
      List<AhatInstance> references = inst.getSoftReverseReferences();
      SubsetSelector<AhatInstance> selector = new SubsetSelector(query, SOFT_REFS_ID, references);
      for (AhatInstance ref : selector.selected()) {
        doc.row(Summarizer.summarize(ref));
      }
      doc.end();
      selector.render(doc);
    }
  }

  private void printAllocationSite(Doc doc, Query query, AhatInstance inst) {
    doc.section("Allocation Site");
    Site site = inst.getSite();
    SitePrinter.printSite(mSnapshot, doc, query, ALLOCATION_SITE_ID, site);
  }

  // Draw the bitmap corresponding to this instance if there is one.
  private static void printBitmap(Doc doc, AhatInstance inst) {
    AhatInstance bitmap = inst.getAssociatedBitmapInstance();
    if (bitmap != null) {
      doc.section("Bitmap Image");
      doc.println(DocString.image(
            DocString.formattedUri("bitmap?id=%d", bitmap.getId()), "bitmap image"));
    }
  }

  private void printGcRootPath(Doc doc, Query query, AhatInstance inst) {
    doc.section("Sample Path from GC Root");
    List<PathElement> path = inst.getPathFromGcRoot();

    // Add 'null' as a marker for the root.
    path.add(0, null);

    HeapTable.TableConfig<PathElement> table = new HeapTable.TableConfig<PathElement>() {
      public String getHeapsDescription() {
        return "Bytes Retained by Heap (Dominators Only)";
      }

      public long getSize(PathElement element, AhatHeap heap) {
        if (element == null) {
          return heap.getSize();
        }
        if (element.isDominator) {
          return element.instance.getRetainedSize(heap);
        }
        return 0;
      }

      public List<HeapTable.ValueConfig<PathElement>> getValueConfigs() {
        HeapTable.ValueConfig<PathElement> value = new HeapTable.ValueConfig<PathElement>() {
          public String getDescription() {
            return "Path Element";
          }

          public DocString render(PathElement element) {
            if (element == null) {
              return DocString.link(DocString.uri("rooted"), DocString.text("ROOT"));
            } else {
              DocString label = DocString.text("→ ");
              label.append(Summarizer.summarize(element.instance));
              label.append(element.field);
              return label;
            }
          }
        };
        return Collections.singletonList(value);
      }
    };
    HeapTable.render(doc, query, DOMINATOR_PATH_ID, table, mSnapshot, path);
  }

  public void printDominatedObjects(Doc doc, Query query, AhatInstance inst) {
    doc.section("Immediately Dominated Objects");
    List<AhatInstance> instances = inst.getDominated();
    if (instances != null) {
      DominatedList.render(mSnapshot, doc, query, DOMINATED_OBJECTS_ID, instances);
    } else {
      doc.println(DocString.text("(none)"));
    }
  }
}

