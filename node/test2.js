var EmiNet = require('./eminet');

var es = EmiNet.open({ fabricatedPacketDropRate: 0.2 }),
    es2 = EmiNet.open({
      acceptConnections: true, port: 2345,
      fabricatedPacketDropRate: 0.2,
      rateLimit: 1000
    });

es2.on('connection', function(socket) {
  console.log("Got connection");
  
  socket.on('disconnect', function(reason) {
    console.log("Server lost connection", reason);
  });
  
  socket.on('message', function(channelQualifier, buf) {
    console.log("                               Server got message", buf.toString());
  });
  
  es2.connect(socket.getAddress(), 2312, function(err, socket) {
    if (err) {
      console.log("ERR:", err);
      return;
    }
    
    console.log("Connected to client as a server");
  
    socket.on('message', function(channelQualifier, buf) {
      console.log("Server client got message", buf.toString());
    });
    
    var counter = 0;
    setInterval(function() {
      var m = 'server client '+(counter += 1);
      socket.send(new Buffer(m), { channelQualifier: EmiNet.channelQualifier(EmiNet.RELIABLE_ORDERED) });
    }, 300);
  })
});

var open = function(msg) {
  es.connect('127.0.0.1', 2345, function(err, socket) {
    if (err) {
      // TODO This callback seems to be called more than once per call to connect, first with success, then with errors.
      console.log("ERR:", err);
      return;
    }
    
    console.log("Connected ("+msg+")");
    
    socket.on('disconnect', function(reason) {
      console.log("Client lost connection ("+msg+")", reason);
    });
    
    socket.on('message', function(channelQualifier, buf, offset, len) {
      console.log("Client got message", buf.toString('utf8', offset, offset+len));
    });
    
    socket.on('lost', function() {
      console.log("LOST!");
    });
    
    socket.on('regained', function() {
      console.log("REGAINED!");
    });
    
    var counter = 0;
    setInterval(function() {
      var m = msg+' '+(counter += 1);
      socket.send(new Buffer(m));
    }, 300);
    
    //setTimeout(function() {
    //  socket.close();
    //}, 500);
  });
};

open('a');
open('b');
open('c');
open('d');
open('1');
open('2');
open('3');
open('4');
open('A');
open('B');
open('C');
open('D');
