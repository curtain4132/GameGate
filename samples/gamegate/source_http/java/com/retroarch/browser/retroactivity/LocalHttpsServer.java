package com.retroarch.browser.retroactivity;

import com.sun.net.httpserver.HttpExchange;
import com.sun.net.httpserver.HttpHandler;
import com.sun.net.httpserver.HttpsConfigurator;
import com.sun.net.httpserver.HttpsParameters;
import com.sun.net.httpserver.HttpsServer;

import java.io.FileInputStream;
import java.io.IOException;
import java.io.OutputStream;
import java.net.InetAddress;
import java.net.InetSocketAddress;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.security.KeyStore;
import java.util.concurrent.Executors;

import javax.net.ssl.KeyManagerFactory;
import javax.net.ssl.SSLContext;

public final class LocalHttpsServer
{
  private LocalHttpsServer()
  {
  }

  public static void main(String[] args) throws Exception
  {
    KeyStore store = KeyStore.getInstance("PKCS12");
    try (FileInputStream input = new FileInputStream(Paths.get(args[0]).toFile()))
    {
      store.load(input, "changeit".toCharArray());
    }
    KeyManagerFactory managers = KeyManagerFactory.getInstance(
        KeyManagerFactory.getDefaultAlgorithm());
    managers.init(store, "changeit".toCharArray());
    SSLContext context = SSLContext.getInstance("TLS");
    context.init(managers.getKeyManagers(), null, null);
    HttpsServer server = HttpsServer.create(new InetSocketAddress(
        InetAddress.getByName("127.0.0.1"), 0), 0);
    server.setHttpsConfigurator(new HttpsConfigurator(context)
    {
      @Override
      public void configure(HttpsParameters parameters)
      {
        super.configure(parameters);
        parameters.setNeedClientAuth(false);
        parameters.setWantClientAuth(false);
      }
    });
    server.setExecutor(Executors.newCachedThreadPool());
    server.createContext("/", new HttpHandler()
    {
      @Override
      public void handle(HttpExchange exchange) throws IOException
      {
        byte[] body = "{\"schema_version\":1}"
            .getBytes(StandardCharsets.UTF_8);
        exchange.sendResponseHeaders(200, 0);
        try (OutputStream output = exchange.getResponseBody())
        {
          output.write(body);
        }
        exchange.close();
      }
    });
    server.start();
    Path portFile = Paths.get(args[1]);
    Files.write(portFile, Integer.toString(
        server.getAddress().getPort()).getBytes(StandardCharsets.US_ASCII));
    Thread.sleep(Long.MAX_VALUE);
  }
}
