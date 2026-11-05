package com.retroarch.browser.retroactivity;

import com.sun.net.httpserver.HttpExchange;
import com.sun.net.httpserver.HttpHandler;
import com.sun.net.httpserver.HttpsConfigurator;
import com.sun.net.httpserver.HttpsServer;

import java.io.ByteArrayOutputStream;
import java.io.FileInputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.net.InetAddress;
import java.net.InetSocketAddress;
import java.lang.reflect.Method;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.security.KeyStore;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.zip.Deflater;
import java.util.zip.GZIPOutputStream;

import javax.net.ssl.KeyManagerFactory;
import javax.net.ssl.SSLContext;

public final class GameGateSourceRequestTest
{
  private static final byte[] CATALOG =
      ("{\"schema_version\":1,\"source\":{\"id\":\"test\","
      + "\"name\":\"Synthetic\"},\"games\":[]}")
      .getBytes(StandardCharsets.UTF_8);
  private static int failures;
  private static int passes;
  private static Path certificates;

  private static final class Server implements AutoCloseable
  {
    final HttpsServer https;
    final ExecutorService executor;

    Server(Path keyStore) throws Exception
    {
      SSLContext context = serverContext(keyStore);
      executor = Executors.newCachedThreadPool();
      https = HttpsServer.create(new InetSocketAddress(
          InetAddress.getByName("127.0.0.1"), 0), 0);
      https.setHttpsConfigurator(new HttpsConfigurator(context));
      https.setExecutor(executor);
      https.createContext("/", new Handler());
      https.start();
    }

    String url(String path)
    {
      return "https://127.0.0.1:" + https.getAddress().getPort() + path;
    }

    @Override
    public void close()
    {
      https.stop(0);
      executor.shutdownNow();
    }
  }

  private static final class Handler implements HttpHandler
  {
    @Override
    public void handle(HttpExchange exchange) throws IOException
    {
      String path = exchange.getRequestURI().getPath();
      try
      {
        if ("/ok".equals(path))
          respond(exchange, CATALOG, 0, null);
        else if ("/gzip".equals(path))
          respond(exchange, gzip(CATALOG), 0, "gzip");
        else if ("/a/".equals(path))
          respond(exchange, CATALOG, 0, null);
        else if ("/a/b.json".equals(path))
          redirect(exchange, ".");
        else if ("/a/b/c.json".equals(path))
          redirect(exchange, "..");
        else if (path.startsWith("/status/"))
        {
          exchange.getResponseHeaders().set("Location", "/ok");
          exchange.sendResponseHeaders(Integer.parseInt(path.substring(8)), -1);
        }
        else if ("/exact".equals(path))
          respond(exchange, new byte[4 * 1024 * 1024], 0, null);
        else if ("/large".equals(path))
          respond(exchange, new byte[4 * 1024 * 1024 + 1], 0, null);
        else if ("/deflate".equals(path))
        {
          byte[] body = new byte[32768];
          byte[] compressed = deflate(body);
          respond(exchange, compressed, compressed.length, "deflate");
        }
        else if ("/expand".equals(path))
        {
          byte[] compressed = gzip(new byte[4 * 1024 * 1024 + 1]);
          respond(exchange, compressed, compressed.length, "gzip");
        }
        else if ("/delay".equals(path))
        {
          sleep(2000);
          respond(exchange, CATALOG, 0, null);
        }
        else if (path.startsWith("/r") && path.length() > 2 &&
            Character.isDigit(path.charAt(2)))
        {
          int index = Integer.parseInt(path.substring(2));
          redirect(exchange, index < 4 ? "/r" + (index + 1) : "/ok");
        }
        else if (path.startsWith("/too"))
        {
          int index = Integer.parseInt(path.substring(4));
          redirect(exchange, index < 5 ? "/too" + (index + 1) : "/ok");
        }
        else if ("/downgrade".equals(path))
          exchange.getResponseHeaders().set("Location", "http://127.0.0.1/");
        else if ("/userinfo".equals(path))
          exchange.getResponseHeaders().set("Location",
              "https://user@127.0.0.1/ok");
        else if ("/fragment".equals(path))
          exchange.getResponseHeaders().set("Location", "/ok#fragment");
        else if ("/rel".equals(path))
          redirect(exchange, "ok");
        else if ("/dot".equals(path))
          redirect(exchange, "/a/../ok");
        else if ("/network".equals(path))
          redirect(exchange, "//127.0.0.1:" +
              exchange.getLocalAddress().getPort() + "/ok");
        else if ("/queryredirect".equals(path))
          redirect(exchange, "/query?token=a%2Fb");
        else if ("/query".equals(path))
        {
          if ("token=a%2Fb".equals(exchange.getRequestURI().getRawQuery()))
            respond(exchange, CATALOG, 0, null);
          else
            exchange.sendResponseHeaders(400, -1);
        }
        else
          exchange.sendResponseHeaders(404, -1);
        if ("/downgrade".equals(path) || "/userinfo".equals(path) ||
            "/fragment".equals(path))
          exchange.sendResponseHeaders(302, -1);
      }
      finally
      {
        exchange.close();
      }
    }
  }

  public static void main(String[] args) throws Exception
  {
    if (args.length != 2)
      throw new IllegalArgumentException("certificate directory and vectors required");
    certificates = Paths.get(args[0]);
    testValidation();
    testRedirectVectors(Paths.get(args[1]));
    try (Server trusted = new Server(certificates.resolve("server.p12"));
         Server untrusted = new Server(certificates.resolve("wrong.p12")))
    {
      testResponses(trusted);
      testRedirects(trusted);
      testTlsFailure(untrusted);
      testCancellationAndTimeout(trusted);
    }
    System.out.println("Java source HTTP assertions: " + passes +
        " passed, " + failures + " failed; no skipped cases");
    if (failures != 0)
    {
      System.err.println(failures + " Java source HTTP test failure(s)");
      System.exit(1);
    }
    System.out.println("Java source HTTP tests passed");
  }

  private static void testValidation()
  {
    check(GameGateSourceRequest.isAllowedUrl(
        "https://example.org/catalog.json?token=a%2Fb"),
        "valid HTTPS URL");
    check(!GameGateSourceRequest.isAllowedUrl(
        "http://example.org/catalog.json"), "HTTP URL rejected");
    check(!GameGateSourceRequest.isAllowedUrl(
        "https://user@example.org/catalog.json"), "userinfo rejected");
    check(!GameGateSourceRequest.isAllowedUrl(
        "https://example.org/catalog.json#fragment"), "fragment rejected");
    check(!GameGateSourceRequest.isAllowedUrl(
        "https://example.org/catalog json"), "space rejected");
    check(!GameGateSourceRequest.isAllowedUrl(
        "https://example.org:0/catalog.json"), "zero port rejected");
  }

  private static void testResponses(Server server) throws Exception
  {
    GameGateSourceRequest request = run(server.url("/ok"), 5000, 10000);
    check(request.getState() == GameGateSourceRequest.STATE_DONE,
        "valid HTTPS response");
    check(request.getError() == GameGateSourceRequest.ERROR_NONE,
        "valid response has no error");
    check(request.getData() != null &&
        contains(request.getData(), "\"schema_version\":1"),
        "valid response body");
    request.cancel();
    check(request.getState() == GameGateSourceRequest.STATE_DONE &&
        request.getData() != null, "cancel after completion keeps data");

    request = run(server.url("/exact"), 5000, 10000);
    check(request.getState() == GameGateSourceRequest.STATE_DONE &&
        request.getData() != null &&
        request.getData().length == 4 * 1024 * 1024,
        "chunked 4 MiB response succeeds");

    request = run(server.url("/large"), 5000, 10000);
    check(request.getState() == GameGateSourceRequest.STATE_ERROR &&
        request.getError() == GameGateSourceRequest.ERROR_TOO_LARGE &&
        request.getData() == null, "4 MiB plus one rejected without data");

    request = run(server.url("/expand"), 5000, 10000);
    check(request.getState() == GameGateSourceRequest.STATE_ERROR &&
        request.getError() == GameGateSourceRequest.ERROR_TOO_LARGE &&
        request.getData() == null, "gzip expansion limit");

    request = run(server.url("/gzip"), 5000, 10000);
    check(request.getState() == GameGateSourceRequest.STATE_DONE &&
        request.getData() != null &&
        contains(request.getData(), "\"schema_version\":1"), "gzip decoding");
    for (int i = 0; i < 64; i++)
    {
      request = run(server.url("/deflate"), 5000, 10000);
      check(request.getState() == GameGateSourceRequest.STATE_DONE &&
          request.getData() != null &&
          request.getData().length == 32768, "repeated deflate decoding");
    }
  }

  private static void testRedirectVectors(Path vectors) throws Exception
  {
    Method resolve = GameGateSourceRequest.class.getDeclaredMethod(
        "resolveRedirect", String.class, String.class);
    resolve.setAccessible(true);
    for (String line : Files.readAllLines(vectors, StandardCharsets.US_ASCII))
    {
      String[] fields = line.split("\t", -1);
      check(fields.length == 3, "shared vector format");
      if (fields.length == 3)
        check(fields[2].equals(resolve.invoke(null, fields[0], fields[1])),
            "shared redirect vector");
    }
    String base = "https://example.org";
    StringBuilder location = new StringBuilder();
    for (int i = 0; i < 2048 - base.length() - 1; i++)
      location.append('a');
    String resolved = (String)resolve.invoke(null, base, location.toString());
    check(resolved != null && resolved.length() == 2048, "exact URL limit");
    location.append('a');
    check(resolve.invoke(null, base, location.toString()) == null,
        "final URL over limit");
    while (location.length() < 2048)
      location.append('a');
    check(resolve.invoke(null, base, location.toString()) == null,
        "maximum location with missing slash");
    location.append('a');
    check(resolve.invoke(null, base, location.toString()) == null,
        "oversized location rejected");
    String longBase = base + "/" +
        location.substring(0, 2048 - base.length() - 3) + "/b";
    check(longBase.length() == 2048 &&
        (base + "/ok").equals(resolve.invoke(null, longBase, "../ok")),
        "oversized intermediate path normalizes within limit");
    check(resolve.invoke(null, longBase + "a", ".") == null,
        "oversized base rejected");
  }

  private static void testRedirects(Server server) throws Exception
  {
    int[] redirectStatuses = {301, 302, 303, 307, 308};
    for (int status : redirectStatuses)
    {
      GameGateSourceRequest redirect = run(server.url("/status/" + status),
          5000, 10000);
      check(redirect.getState() == GameGateSourceRequest.STATE_DONE,
          "allowed redirect " + status);
    }
    int[] rejectedStatuses = {300, 304, 305, 306, 309};
    for (int status : rejectedStatuses)
    {
      GameGateSourceRequest rejected = run(server.url("/status/" + status),
          5000, 10000);
      check(rejected.getState() == GameGateSourceRequest.STATE_ERROR &&
          rejected.getError() == GameGateSourceRequest.ERROR_HTTP_STATUS &&
          rejected.getData() == null, "non-redirect status " + status);
    }
    for (String path : new String[] {"/a/b.json", "/a/b/c.json"})
    {
      GameGateSourceRequest directory = run(server.url(path), 5000, 10000);
      check(directory.getState() == GameGateSourceRequest.STATE_DONE &&
          directory.getData() != null &&
          contains(directory.getData(), "\"schema_version\":1"),
          "dot redirect preserves directory slash");
    }
    GameGateSourceRequest request = run(server.url("/r0"), 5000, 10000);
    check(request.getState() == GameGateSourceRequest.STATE_DONE,
        "five redirects succeed");
    request = run(server.url("/too0"), 5000, 10000);
    check(request.getState() == GameGateSourceRequest.STATE_ERROR &&
        request.getError() == GameGateSourceRequest.ERROR_REDIRECT,
        "six redirects rejected");
    request = run(server.url("/downgrade"), 5000, 10000);
    check(request.getState() == GameGateSourceRequest.STATE_ERROR &&
        request.getError() == GameGateSourceRequest.ERROR_REDIRECT,
        "HTTP downgrade rejected");
    request = run(server.url("/userinfo"), 5000, 10000);
    check(request.getState() == GameGateSourceRequest.STATE_ERROR &&
        request.getError() == GameGateSourceRequest.ERROR_REDIRECT,
        "redirect userinfo rejected");
    request = run(server.url("/fragment"), 5000, 10000);
    check(request.getState() == GameGateSourceRequest.STATE_ERROR &&
        request.getError() == GameGateSourceRequest.ERROR_REDIRECT,
        "redirect fragment rejected");
    request = run(server.url("/rel"), 5000, 10000);
    check(request.getState() == GameGateSourceRequest.STATE_DONE,
        "relative redirect");
    request = run(server.url("/dot"), 5000, 10000);
    check(request.getState() == GameGateSourceRequest.STATE_DONE,
        "dot-segment redirect");
    request = run(server.url("/network"), 5000, 10000);
    check(request.getState() == GameGateSourceRequest.STATE_DONE,
        "network-path redirect");
    request = run(server.url("/queryredirect"), 5000, 10000);
    check(request.getState() == GameGateSourceRequest.STATE_DONE,
        "redirect query preserved");
  }

  private static void testTlsFailure(Server server) throws Exception
  {
    GameGateSourceRequest request = run(server.url("/ok"), 3000, 10000);
    check(request.getState() == GameGateSourceRequest.STATE_ERROR &&
        request.getError() == GameGateSourceRequest.ERROR_TLS &&
        request.getData() == null, "untrusted certificate rejected");
  }

  private static void testCancellationAndTimeout(Server server)
      throws Exception
  {
    GameGateSourceRequest request =
        GameGateSourceRequest.start(server.url("/delay"), 5000);
    request.cancel();
    await(request, 3000);
    check(request.getState() == GameGateSourceRequest.STATE_CANCELLED &&
        request.getData() == null, "cancellation exposes no data");

    request = GameGateSourceRequest.start(server.url("/delay"), 300);
    await(request, 5000);
    check(request.getState() == GameGateSourceRequest.STATE_ERROR &&
        request.getError() == GameGateSourceRequest.ERROR_TIMEOUT &&
        request.getData() == null, "deadline includes blocked response");
  }

  private static GameGateSourceRequest run(String url, long timeout,
      long waitMillis) throws Exception
  {
    GameGateSourceRequest request =
        GameGateSourceRequest.start(url, timeout);
    await(request, waitMillis);
    return request;
  }

  private static void await(GameGateSourceRequest request, long waitMillis)
      throws InterruptedException
  {
    long deadline = System.nanoTime() + waitMillis * 1000000L;
    while (request.getState() == GameGateSourceRequest.STATE_WORKING &&
        System.nanoTime() - deadline < 0)
      Thread.sleep(10);
  }

  private static SSLContext serverContext(Path path) throws Exception
  {
    KeyStore store = KeyStore.getInstance("PKCS12");
    try (FileInputStream input = new FileInputStream(path.toFile()))
    {
      store.load(input, "changeit".toCharArray());
    }
    KeyManagerFactory managers = KeyManagerFactory.getInstance(
        KeyManagerFactory.getDefaultAlgorithm());
    managers.init(store, "changeit".toCharArray());
    SSLContext context = SSLContext.getInstance("TLS");
    context.init(managers.getKeyManagers(), null, null);
    return context;
  }

  private static byte[] gzip(byte[] source) throws IOException
  {
    ByteArrayOutputStream output = new ByteArrayOutputStream();
    try (GZIPOutputStream gzip = new GZIPOutputStream(output))
    {
      gzip.write(source);
    }
    return output.toByteArray();
  }

  private static byte[] deflate(byte[] source)
  {
    Deflater deflater = new Deflater();
    deflater.setInput(source);
    deflater.finish();
    ByteArrayOutputStream output = new ByteArrayOutputStream();
    byte[] buffer = new byte[4096];
    while (!deflater.finished())
    {
      int count = deflater.deflate(buffer);
      output.write(buffer, 0, count);
    }
    deflater.end();
    return output.toByteArray();
  }

  private static void respond(HttpExchange exchange, byte[] body,
      long contentLength, String encoding) throws IOException
  {
    if (encoding != null)
      exchange.getResponseHeaders().set("Content-Encoding", encoding);
    exchange.sendResponseHeaders(200, contentLength);
    try (OutputStream output = exchange.getResponseBody())
    {
      output.write(body);
    }
  }

  private static void redirect(HttpExchange exchange, String location)
      throws IOException
  {
    exchange.getResponseHeaders().set("Location", location);
    exchange.sendResponseHeaders(302, -1);
  }

  private static boolean contains(byte[] data, String text)
  {
    byte[] needle = text.getBytes(StandardCharsets.UTF_8);
    outer:
    for (int i = 0; i <= data.length - needle.length; i++)
    {
      for (int j = 0; j < needle.length; j++)
        if (data[i + j] != needle[j])
          continue outer;
      return true;
    }
    return false;
  }

  private static void sleep(long millis)
  {
    try
    {
      Thread.sleep(millis);
    }
    catch (InterruptedException interruption)
    {
      Thread.currentThread().interrupt();
    }
  }

  private static void check(boolean condition, String name)
  {
    if (!condition)
    {
      System.err.println("FAIL: " + name);
      failures++;
    }
    else
      passes++;
  }
}
