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
import com.android.ahat.heapdump.Diff;
import com.android.tools.perflib.heap.ProguardMap;
import com.sun.net.httpserver.HttpServer;
import java.io.File;
import java.io.IOException;
import java.io.PrintStream;
import java.net.InetAddress;
import java.net.InetSocketAddress;
import java.text.ParseException;
import java.util.concurrent.Executors;

public class Main {

  public static void help(PrintStream out) {
    out.println("java -jar ahat.jar [OPTIONS] FILE");
    out.println("  Launch an http server for viewing the given Android heap dump FILE.");
    out.println("");
    out.println("OPTIONS:");
    out.println("  -p <port>");
    out.println("     Serve pages on the given port. Defaults to 7100.");
    out.println("  --proguard-map FILE");
    out.println("     Use the proguard map FILE to deobfuscate the heap dump.");
    out.println("  --baseline FILE");
    out.println("     Diff the heap dump against the given baseline heap dump FILE.");
    out.println("  --baseline-proguard-map FILE");
    out.println("     Use the proguard map FILE to deobfuscate the baseline heap dump.");
    out.println("");
  }

  public static void main(String[] args) throws IOException {
    int port = 7100;
    for (String arg : args) {
      if (arg.equals("--help")) {
        help(System.out);
        return;
      }
    }

    File hprof = null;
    File hprofbase = null;
    ProguardMap map = new ProguardMap();
    ProguardMap mapbase = new ProguardMap();
    for (int i = 0; i < args.length; i++) {
      if ("-p".equals(args[i]) && i + 1 < args.length) {
        i++;
        port = Integer.parseInt(args[i]);
      } else if ("--proguard-map".equals(args[i]) && i + 1 < args.length) {
        i++;
        try {
          map.readFromFile(new File(args[i]));
        } catch (IOException|ParseException ex) {
          System.out.println("Unable to read proguard map: " + ex);
          System.out.println("The proguard map will not be used.");
        }
      } else if ("--baseline-proguard-map".equals(args[i]) && i + 1 < args.length) {
        i++;
        try {
          mapbase.readFromFile(new File(args[i]));
        } catch (IOException|ParseException ex) {
          System.out.println("Unable to read baselline proguard map: " + ex);
          System.out.println("The proguard map will not be used.");
        }
      } else if ("--baseline".equals(args[i]) && i + 1 < args.length) {
        i++;
        if (hprofbase != null) {
          System.err.println("multiple baseline heap dumps.");
          help(System.err);
          return;
        }
        hprofbase = new File(args[i]);
      } else {
        if (hprof != null) {
          System.err.println("multiple input files.");
          help(System.err);
          return;
        }
        hprof = new File(args[i]);
      }
    }

    if (hprof == null) {
      System.err.println("no input file.");
      help(System.err);
      return;
    }

    // Launch the server before parsing the hprof file so we get
    // BindExceptions quickly.
    InetAddress loopback = InetAddress.getLoopbackAddress();
    InetSocketAddress addr = new InetSocketAddress(loopback, port);
    HttpServer server = HttpServer.create(addr, 0);

    System.out.println("Processing hprof file...");
    AhatSnapshot ahat = AhatSnapshot.fromHprof(hprof, map);

    if (hprofbase != null) {
      System.out.println("Processing baseline hprof file...");
      AhatSnapshot base = AhatSnapshot.fromHprof(hprofbase, mapbase);

      System.out.println("Diffing hprof files...");
      Diff.snapshots(ahat, base);
    }

    server.createContext("/", new AhatHttpHandler(new OverviewHandler(ahat, hprof, hprofbase)));
    server.createContext("/rooted", new AhatHttpHandler(new RootedHandler(ahat)));
    server.createContext("/object", new AhatHttpHandler(new ObjectHandler(ahat)));
    server.createContext("/objects", new AhatHttpHandler(new ObjectsHandler(ahat)));
    server.createContext("/site", new AhatHttpHandler(new SiteHandler(ahat)));
    server.createContext("/bitmap", new BitmapHandler(ahat));
    server.createContext("/style.css", new StaticHandler("style.css", "text/css"));
    server.setExecutor(Executors.newFixedThreadPool(1));
    System.out.println("Server started on localhost:" + port);

    server.start();
  }
}

