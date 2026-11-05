package com.retroarch.browser.retroactivity;

import java.io.BufferedInputStream;
import java.io.ByteArrayOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.net.HttpURLConnection;
import java.net.SocketTimeoutException;
import java.net.URL;
import java.util.Locale;
import java.util.concurrent.atomic.AtomicReference;
import java.util.zip.GZIPInputStream;
import java.util.zip.InflaterInputStream;
import java.util.zip.ZipException;

import javax.net.ssl.HttpsURLConnection;
import javax.net.ssl.SSLException;

/* Java owns the blocking worker; native callers only poll volatile state. */
public final class GameGateSourceRequest implements Runnable
{
  /* These integer values cross the JNI boundary. */
  public static final int STATE_WORKING = 0;
  public static final int STATE_DONE = 1;
  public static final int STATE_ERROR = 2;
  public static final int STATE_CANCELLED = 3;
  public static final int STATE_UNSUPPORTED = 4;

  public static final int ERROR_NONE = 0;
  public static final int ERROR_INVALID_ARGUMENT = 1;
  public static final int ERROR_INVALID_URL = 2;
  public static final int ERROR_OUT_OF_MEMORY = 3;
  public static final int ERROR_NETWORK = 4;
  public static final int ERROR_TLS = 5;
  public static final int ERROR_HTTP_STATUS = 6;
  public static final int ERROR_REDIRECT = 7;
  public static final int ERROR_TOO_LARGE = 8;
  public static final int ERROR_DECOMPRESSION = 9;
  public static final int ERROR_CANCELLED = 10;
  public static final int ERROR_TIMEOUT = 11;
  public static final int ERROR_UNSUPPORTED = 12;
  public static final int ERROR_INTERNAL = 13;

  private static final int MAX_URL_BYTES = 2048;
  private static final int MAX_BYTES = 4 * 1024 * 1024;
  private static final int MAX_REDIRECTS = 5;
  private static final int READ_BUFFER_BYTES = 16384;

  private static final class Result
  {
    final int state;
    final int error;
    final byte[] data;

    Result(int state, int error, byte[] data)
    {
      this.state = state;
      this.error = error;
      this.data = data;
    }
  }

  private static final Result WORKING_RESULT =
      new Result(STATE_WORKING, ERROR_NONE, null);
  private static final Result CANCELLED_RESULT =
      new Result(STATE_CANCELLED, ERROR_CANCELLED, null);

  private final String url;
  private final long deadlineNanos;
  private final byte[] readBuffer = new byte[READ_BUFFER_BYTES];
  private final AtomicReference<Result> result =
      new AtomicReference<Result>(WORKING_RESULT);
  private volatile boolean cancelled;
  private Thread thread;

  private static final class RequestFailure extends Exception
  {
    private static final long serialVersionUID = 1L;
    final int code;

    RequestFailure(int code)
    {
      this.code = code;
    }
  }

  private GameGateSourceRequest(String url, long timeoutMillis)
  {
    this.url = url;
    long timeout = timeoutMillis > 0 ? timeoutMillis : 0;
    this.deadlineNanos = System.nanoTime() + timeout * 1000000L;
  }

  public static GameGateSourceRequest start(String url, long timeoutMillis)
  {
    GameGateSourceRequest request;
    Thread worker;
    if (!isAllowedUrl(url))
    {
      request = new GameGateSourceRequest("", 0);
      request.finish(STATE_ERROR, ERROR_INVALID_URL, null);
      return request;
    }
    if (timeoutMillis <= 0)
    {
      request = new GameGateSourceRequest("", 0);
      request.finish(STATE_ERROR, ERROR_INVALID_ARGUMENT, null);
      return request;
    }
    request = new GameGateSourceRequest(url, timeoutMillis);
    worker = new Thread(request, "GameGateSourceRequest");
    worker.setDaemon(true);
    request.thread = worker;
    worker.start();
    return request;
  }

  public int getState()
  {
    return result.get().state;
  }

  public int getError()
  {
    return result.get().error;
  }

  public byte[] getData()
  {
    Result current = result.get();
    return current.state == STATE_DONE ? current.data : null;
  }

  public void cancel()
  {
    Result current;
    cancelled = true;
    current = result.get();
    while (current.state == STATE_WORKING)
    {
      if (result.compareAndSet(current, CANCELLED_RESULT))
        return;
      current = result.get();
    }
  }

  @Override
  public void run()
  {
    try
    {
      fetch();
    }
    catch (RequestFailure failure)
    {
      finish(STATE_ERROR, failure.code, null);
    }
    catch (ZipException failure)
    {
      finish(STATE_ERROR, ERROR_DECOMPRESSION, null);
    }
    catch (SocketTimeoutException failure)
    {
      finish(STATE_ERROR, ERROR_TIMEOUT, null);
    }
    catch (SSLException failure)
    {
      finish(STATE_ERROR, ERROR_TLS, null);
    }
    catch (IOException failure)
    {
      finish(STATE_ERROR, isTlsFailure(failure)
          ? ERROR_TLS : ERROR_NETWORK, null);
    }
    catch (OutOfMemoryError failure)
    {
      finish(STATE_ERROR, ERROR_OUT_OF_MEMORY, null);
    }
    catch (Throwable failure)
    {
      finish(STATE_ERROR, ERROR_INTERNAL, null);
    }
  }

  private void fetch() throws IOException, RequestFailure
  {
    String current = url;
    int redirects = 0;
    for (;;)
    {
      HttpsURLConnection connection;
      InputStream input;
      byte[] body;
      int status;
      String location;
      checkActive();
      connection = open(current);
      try
      {
        connection.connect();
        connection.setReadTimeout(remainingMillis());
        status = connection.getResponseCode();
        checkActive();
        if (status == 301 || status == 302 || status == 303 ||
            status == 307 || status == 308)
        {
          location = connection.getHeaderField("Location");
          if (redirects >= MAX_REDIRECTS || location == null ||
              (current = resolveRedirect(current, location)) == null)
            throw new RequestFailure(ERROR_REDIRECT);
          redirects++;
          continue;
        }
        if (status < 200 || status >= 300)
          throw new RequestFailure(ERROR_HTTP_STATUS);
        try
        {
          input = decodedInput(connection);
        }
        catch (IllegalArgumentException failure)
        {
          throw new RequestFailure(ERROR_UNSUPPORTED);
        }
        body = readBounded(input, connection);
        finish(STATE_DONE, ERROR_NONE, body);
        return;
      }
      finally
      {
        connection.disconnect();
      }
    }
  }

  private HttpsURLConnection open(String current)
      throws IOException, RequestFailure
  {
    URL parsed = new URL(current);
    HttpURLConnection raw = (HttpURLConnection) parsed.openConnection();
    HttpsURLConnection connection;
    int timeout;
    if (!(raw instanceof HttpsURLConnection))
      throw new RequestFailure(ERROR_REDIRECT);
    connection = (HttpsURLConnection) raw;
    timeout = remainingMillis();
    connection.setInstanceFollowRedirects(false);
    connection.setUseCaches(false);
    connection.setConnectTimeout(timeout);
    connection.setReadTimeout(timeout);
    connection.setRequestMethod("GET");
    connection.setRequestProperty("Accept-Encoding", "gzip, deflate");
    return connection;
  }

  private InputStream decodedInput(HttpsURLConnection connection)
      throws IOException, RequestFailure
  {
    InputStream input = connection.getInputStream();
    String encoding = connection.getHeaderField("Content-Encoding");
    if (encoding == null || encoding.trim().length() == 0)
      return input;
    encoding = encoding.trim().toLowerCase(Locale.ROOT);
    if ("gzip".equals(encoding))
      return new BufferedInputStream(new GZIPInputStream(input), 4096);
    if ("deflate".equals(encoding))
      return new BufferedInputStream(new InflaterInputStream(input), 4096);
    if ("identity".equals(encoding))
      return new BufferedInputStream(input, 4096);
    input.close();
    throw new RequestFailure(ERROR_UNSUPPORTED);
  }

  private byte[] readBounded(InputStream input,
      HttpsURLConnection connection)
      throws IOException, RequestFailure
  {
    ByteArrayOutputStream output = new ByteArrayOutputStream(READ_BUFFER_BYTES);
    int count;
    try
    {
      while (true)
      {
        connection.setReadTimeout(remainingMillis());
        count = input.read(readBuffer);
        if (count < 0)
          break;
        checkActive();
        if (count == 0)
          continue;
        if (output.size() > MAX_BYTES - count)
          throw new RequestFailure(ERROR_TOO_LARGE);
        output.write(readBuffer, 0, count);
      }
      checkActive();
      return output.toByteArray();
    }
    finally
    {
      input.close();
    }
  }

  private void checkActive() throws RequestFailure
  {
    if (cancelled)
      throw new RequestFailure(ERROR_CANCELLED);
    if (System.nanoTime() - deadlineNanos >= 0)
      throw new RequestFailure(ERROR_TIMEOUT);
  }

  private int remainingMillis() throws RequestFailure
  {
    long remaining = deadlineNanos - System.nanoTime();
    long milliseconds;
    if (remaining <= 0)
      throw new RequestFailure(ERROR_TIMEOUT);
    milliseconds = (remaining + 999999L) / 1000000L;
    if (milliseconds > Integer.MAX_VALUE)
      milliseconds = Integer.MAX_VALUE;
    return (int)milliseconds;
  }

  private void finish(int finalState, int finalError, byte[] finalData)
  {
    Result next = cancelled ? CANCELLED_RESULT
        : new Result(finalState, finalError, finalData);
    result.compareAndSet(WORKING_RESULT, next);
  }

  private static boolean isTlsFailure(Throwable failure)
  {
    while (failure != null)
    {
      if (failure instanceof SSLException)
        return true;
      failure = failure.getCause();
    }
    return false;
  }

  private static String resolveRedirect(String base, String location)
  {
    int authorityEnd = 8;
    int basePathEnd;
    int locationQuery;
    int locationPathEnd;
    String merged;
    String path;
    String result;
    if (!isAllowedUrl(base) || location == null || location.length() == 0)
      return null;
    if (location.length() > MAX_URL_BYTES)
      return null;
    for (int i = 0; i < location.length(); i++)
    {
      char c = location.charAt(i);
      if (c <= 32 || c >= 127 || c == '\\' || c == '#')
        return null;
    }
    if (startsWithHttps(location))
      return isAllowedUrl(location) ? location : null;
    if (location.startsWith("//"))
    {
      result = "https:" + location;
      return isAllowedUrl(result) ? result : null;
    }
    if (hasScheme(location))
      return null;
    while (authorityEnd < base.length() && base.charAt(authorityEnd) != '/' &&
        base.charAt(authorityEnd) != '?')
      authorityEnd++;
    basePathEnd = authorityEnd;
    while (basePathEnd < base.length() && base.charAt(basePathEnd) != '?')
      basePathEnd++;
    locationQuery = location.indexOf('?');
    locationPathEnd = locationQuery < 0 ? location.length() : locationQuery;
    if (location.charAt(0) == '?')
      merged = basePathEnd == authorityEnd ? "/" :
          base.substring(authorityEnd, basePathEnd);
    else if (location.charAt(0) == '/')
      merged = location.substring(0, locationPathEnd);
    else
    {
      int directory = basePathEnd;
      while (directory > authorityEnd &&
          base.charAt(directory - 1) != '/')
        directory--;
      merged = (directory == authorityEnd ? "/" :
          base.substring(authorityEnd, directory)) +
          location.substring(0, locationPathEnd);
    }
    path = removeDotSegments(merged);
    result = base.substring(0, authorityEnd) + path +
        (locationQuery < 0 ? "" : location.substring(locationQuery));
    return isAllowedUrl(result) ? result : null;
  }

  private static String removeDotSegments(String input)
  {
    StringBuilder output = new StringBuilder(input.length() + 1);
    int in = 0;
    while (in < input.length())
    {
      int remaining = input.length() - in;
      if (input.startsWith("../", in))
        in += 3;
      else if (input.startsWith("./", in))
        in += 2;
      else if (input.startsWith("/./", in))
        in += 2;
      else if (remaining == 2 && input.startsWith("/.", in))
      {
        output.append('/');
        in += 2;
      }
      else if (input.startsWith("/../", in) ||
          (remaining == 3 && input.startsWith("/..", in)))
      {
        int slash = output.lastIndexOf("/");
        output.setLength(slash < 0 ? 0 : slash);
        in += 3;
        if (remaining == 3)
          output.append('/');
      }
      else if ((remaining == 1 && input.charAt(in) == '.') ||
          (remaining == 2 && input.startsWith("..", in)))
        in += remaining;
      else
      {
        int start = in;
        if (input.charAt(in) == '/')
          in++;
        while (in < input.length() && input.charAt(in) != '/')
          in++;
        output.append(input, start, in);
      }
    }
    return output.toString();
  }

  private static boolean startsWithHttps(String text)
  {
    return text.length() >= 8 &&
        text.regionMatches(true, 0, "https://", 0, 8);
  }

  private static boolean hasScheme(String text)
  {
    if (text.length() == 0 ||
        !((text.charAt(0) >= 'A' && text.charAt(0) <= 'Z') ||
          (text.charAt(0) >= 'a' && text.charAt(0) <= 'z')))
      return false;
    for (int i = 1; i < text.length(); i++)
    {
      char c = text.charAt(i);
      if (c == ':')
        return true;
      if (c == '/' || c == '?' || c == '#')
        return false;
      if (!isAsciiAlnum(c) && c != '+' && c != '-' && c != '.')
        return false;
    }
    return false;
  }

  static boolean isAllowedUrl(String text)
  {
    int authorityEnd;
    int hostStart = 8;
    int hostEnd;
    int port = -1;
    if (text == null || text.length() < 9 || text.length() > MAX_URL_BYTES ||
        !startsWithHttps(text))
      return false;
    for (int i = 0; i < text.length(); i++)
    {
      char c = text.charAt(i);
      if (c <= 32 || c >= 127 || c == '\\' || c == '#')
        return false;
    }
    authorityEnd = 8;
    while (authorityEnd < text.length() && text.charAt(authorityEnd) != '/' &&
        text.charAt(authorityEnd) != '?')
      authorityEnd++;
    if (text.charAt(8) == '[')
    {
      hostEnd = text.indexOf(']', 9);
      if (hostEnd < 0 || hostEnd >= authorityEnd ||
          !validIpv6(text, 9, hostEnd))
        return false;
      if (hostEnd + 1 < authorityEnd)
      {
        if (text.charAt(hostEnd + 1) != ':')
          return false;
        port = parsePort(text, hostEnd + 2, authorityEnd);
        if (port == 0)
          return false;
      }
    }
    else
    {
      hostEnd = 8;
      while (hostEnd < authorityEnd && text.charAt(hostEnd) != ':')
        hostEnd++;
      if (!validHost(text, 8, hostEnd))
        return false;
      if (hostEnd < authorityEnd)
      {
        if (text.charAt(hostEnd) != ':')
          return false;
        port = parsePort(text, hostEnd + 1, authorityEnd);
        if (port == 0)
          return false;
      }
    }
    for (int i = authorityEnd; i < text.length(); i++)
    {
      char c = text.charAt(i);
      if (c == '%')
      {
        if (i + 2 >= text.length() || !isHex(text.charAt(i + 1)) ||
            !isHex(text.charAt(i + 2)))
          return false;
        i += 2;
      }
      else if (!isAsciiAlnum(c) &&
          "-._~!$&'()*+,;=:@/?".indexOf(c) < 0)
        return false;
    }
    return true;
  }

  private static int parsePort(String text, int start, int end)
  {
    int value = 0;
    if (start == end)
      return 0;
    for (int i = start; i < end; i++)
    {
      char c = text.charAt(i);
      if (c < '0' || c > '9')
        return 0;
      value = value * 10 + (c - '0');
      if (value > 65535)
        return 0;
    }
    return value;
  }

  private static boolean validIpv4(String text, int start, int end)
  {
    int index = start;
    int parts = 0;
    while (index < end)
    {
      int partStart = index;
      int value = 0;
      while (index < end && text.charAt(index) >= '0' &&
          text.charAt(index) <= '9')
      {
        value = value * 10 + (text.charAt(index) - '0');
        index++;
        if (index - partStart > 3 || value > 255)
          return false;
      }
      if (index == partStart ||
          (index - partStart > 1 && text.charAt(partStart) == '0'))
        return false;
      parts++;
      if (index == end)
        return parts == 4;
      if (text.charAt(index++) != '.' || index == end)
        return false;
    }
    return false;
  }

  private static boolean validIpv6(String text, int start, int end)
  {
    int index = start;
    int groups = 0;
    boolean compressed = false;
    if (start == end)
      return false;
    if (text.charAt(index) == ':')
    {
      if (end - index < 2 || text.charAt(index + 1) != ':')
        return false;
      compressed = true;
      index += 2;
    }
    while (index < end)
    {
      int groupStart = index;
      boolean dot = false;
      while (index < end && text.charAt(index) != ':')
      {
        if (text.charAt(index) == '.')
          dot = true;
        index++;
      }
      if (dot)
        return index == end && validIpv4(text, groupStart, end) &&
            (compressed ? groups + 2 < 8 : groups + 2 == 8);
      if (index == groupStart || index - groupStart > 4)
        return false;
      for (int i = groupStart; i < index; i++)
        if (!isHex(text.charAt(i)))
          return false;
      groups++;
      if (groups > 8)
        return false;
      if (index < end)
      {
        index++;
        if (index < end && text.charAt(index) == ':')
        {
          if (compressed)
            return false;
          compressed = true;
          index++;
        }
        else if (index == end)
          return false;
      }
    }
    return compressed ? groups < 8 : groups == 8;
  }

  private static boolean validHost(String text, int start, int end)
  {
    int length = end - start;
    boolean numeric = true;
    if (length <= 0 || length > 254)
      return false;
    for (int i = start; i < end; i++)
      if (text.charAt(i) != '.' &&
          (text.charAt(i) < '0' || text.charAt(i) > '9'))
        numeric = false;
    if (numeric)
      return validIpv4(text, start, end);
    if (text.charAt(end - 1) == '.')
      end--;
    if (end == start || end - start > 253)
      return false;
    int labelStart = start;
    for (int i = start; i <= end; i++)
    {
      if (i == end || text.charAt(i) == '.')
      {
        if (i == labelStart || i - labelStart > 63 ||
            text.charAt(labelStart) == '-' ||
            text.charAt(i - 1) == '-')
          return false;
        labelStart = i + 1;
      }
      else if (!isAsciiAlnum(text.charAt(i)) && text.charAt(i) != '-')
        return false;
    }
    return true;
  }

  private static boolean isAsciiAlnum(char c)
  {
    char lower = c >= 'A' && c <= 'Z' ? (char)(c - 'A' + 'a') : c;
    return (lower >= 'a' && lower <= 'z') ||
        (lower >= '0' && lower <= '9');
  }

  private static boolean isHex(char c)
  {
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') ||
        (c >= 'A' && c <= 'F');
  }
}
