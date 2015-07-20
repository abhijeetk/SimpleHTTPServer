# SimpleHTTPServer

HTTP server is implemented using socket programming to support GET, HEAD and POST method with basic error handling.

<b>Known Limitations :</b>
<ul>
<li>It supports only GET, HEAD and POST.</li>
<li>It support only text resources. (eg. text/html)</li>
<li>Server creates thread per request so it is heavy.</li>
</ul>

<b>Future work :</b>
<ul>
<li>Add support to remaining HTTP methods as per spec.</li>
<li>Add support images,audio,video,upload.</li>
<li>Use of thread pool and thread safe data structures.</li>
<li>Use of non blocking IO using select() or pselect() APIS.</li>
</ul>

<b>How to build:</b><br>
.../SimpleHTTPServer$ make

<b>How to run:</b><br>
.../SimpleHTTPServer$ ./httpd<br>
httpd running on port 37889<br><br>
To check server is working or not, open http://127.0.1.1:37889/ in browser<br>
<br>
<b>How to test:</b><br>
<ol>
<li>HTTP GET Method : http://127.0.1.1:37889/GET.html </li>
<li>HTTP POST Method : http://127.0.1.1:37889/POST.html </li>
<li>HTTP HEAD Method : <br>
.../SimpleHTTPServer$ curl -i -X HEAD  http://127.0.1.1:37889 <br>
HTTP/1.0 200 OK <br>
Server: SimpleHTTPServer/0.1.0 <br>
Content-Type: text/html<br>
</li>
</ol>

<b>LinkedIn:</b><br>
https://in.linkedin.com/in/abhijeetkandalkar
