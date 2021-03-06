// CXX=clang node-waf configure build && node test.js

var Util = require('util'),
    Events = require('events'),
    EmiNetAddon = require('../build/Release/eminet');

// Lazily loaded
var Dns = null,
    Net = null;

var isIP = function(address) {
    if (!net) net = require('net');
    return net.isIP(address);
};

var lookup = function(address, family, callback) {
    // implicit 'bind before send' needs to run on the same tick
    var matchedFamily = isIP(address);
    if (matchedFamily)
        return callback(null, address, matchedFamily);

    if (!dns)
        dns = require('dns');

    return dns.lookup(address, family, callback);
};

var errnoException = function(errorno, syscall) {
    var e = new Error(syscall+' '+errorno);
    e.errno = e.code = errorno;
    e.syscall = syscall;
    return e;
};

var lookup4 = function(address, callback) {
    return lookup(address || '0.0.0.0', 4, callback);
};

var lookup6 = function(address, callback) {
    return lookup(address || '::0', 6, callback);
};

var gotConnection = function(sock, sockHandle, connHandle) {
  var conn = new EmiConnection(false, sockHandle, connHandle);
  sock && sock.emit('connection', conn);
  return conn;
};

var connectionPacketLoss = function(conn, connHandle, channelQualifier, packetsLost) {
  conn && conn.emit('loss', channelQualifier, packetsLost);
};

var connectionMessage = function(conn, connHandle, channelQualifier, slowBuffer, offset, length) {
  conn && conn.emit('message', channelQualifier, new Buffer(slowBuffer, length, offset));
};

var connectionLost = function(conn, connHandle) {
  conn && conn.emit('lost');
};

var connectionRegained = function(conn, connHandle) {
  conn && conn.emit('regained');
};

var connectionDisconnect = function(conn, connHandle, reason) {
  conn && conn.emit('disconnect', reason);
};

var natPunchthroughFinished = function(conn, connHandle, success) {
  conn && conn.emit('p2p', success ? null : { error: 'Failed to establish P2P connection' });
};

var connectionError = function() {
  // TODO
  console.log("!!! Connection error", arguments);
};

var p2pSockError = function() {
  // TODO
  console.log("!!! P2P Socket error", arguments);
}

EmiNetAddon.setCallbacks(
  gotConnection,
  connectionPacketLoss,
  connectionMessage,
  connectionLost,
  connectionRegained,
  connectionDisconnect,
  natPunchthroughFinished,
  connectionError
);

EmiNetAddon.setP2PCallbacks(
    p2pSockError
);


var EmiConnection = function(initiator, sockHandle, address, port, cb, p2pCookie, sharedSecret) {
  if (initiator) {
    var self = this;
    
    var type = this.type || 'udp4';
    
    var wrappedCb = function(err, conn) {
      self._handle = conn;
      
      // We want to wait with invoking cb until this function
      // has returned, to make sure that the C++ wrapper code
      // has access to the self object when cb is invoked,
      // which might be necessary for instance if the callback
      // invokes forceClose.
      process.nextTick(function() {
        cb(err, conn && self);
      });
      
      return self;
    };
    
    var fn;
    if ('udp4' == type) {
      fn = 'connect4';
    }
    else if ('udp6' == type) {
      fn = 'connect6';
    }
    else {
        throw new Error('Bad socket type. Valid types: udp4, udp6');
    }
    
    if (typeof p2pCookie != 'undefined' || typeof sharedSecret != 'undefined') {
      sockHandle[fn](address, port, wrappedCb, new Buffer(p2pCookie), new Buffer(sharedSecret));
    }
    else {
      sockHandle[fn](address, port, wrappedCb);
    }
  }
  else {
    this._handle = address;
  }
};

Util.inherits(EmiConnection, Events.EventEmitter);

[
  'close', 'forceClose', 'closeOrForceClose', 'send',
  'hasIssuedConnectionWarning', 'getSocket', 'getAddressType',
  'getLocalPort', 'getLocalAddress', 'getRemoteAddress',
  'getRemotePort', 'getInboundPort', 'isOpen', 'isOpening',
  'getP2PState'
].forEach(function(name) {
  EmiConnection.prototype[name] = function() {
    return this._handle[name].apply(this._handle, arguments);
  };
});


var EmiSocket = function(args) {
  this._handle = new EmiNetAddon.EmiSocket(this, args);
  
  for (var key in args) {
    this[key] = args[key];
  }
};

Util.inherits(EmiSocket, Events.EventEmitter);

EmiSocket.prototype.connect = function(address, port, cb) {
  return new EmiConnection(/*initiator:*/true, this._handle, address, port, cb);
};

EmiSocket.prototype.connectP2P = function(address, port, p2pCookie, sharedSecret, cb) {
  return new EmiConnection(/*initiator:*/true, this._handle, address, port, cb, p2pCookie, sharedSecret);
};


var EmiP2PSocket = function(args) {
  this._handle = new EmiNetAddon.EmiP2PSocket(this, args);
  
  for (var key in args) {
    this[key] = args[key];
  }
};

Util.inherits(EmiP2PSocket, Events.EventEmitter);

[
  'getAddressType', 'getPort', 'getAddress'
].forEach(function(name) {
  EmiP2PSocket.prototype[name] = function() {
    return this._handle[name].apply(this._handle, arguments);
  };
});

EmiP2PSocket.prototype.generateCookiePair = function() {
  var ret = this._handle.generateCookiePair.apply(this._handle, arguments);
  if (ret) {
    var buf = new Buffer(ret);
    return [buf.slice(0, buf.length/2), buf.slice(buf.length/2, buf.length)];
  }
  else {
    return ret;
  }
};

EmiP2PSocket.prototype.generateSharedSecret = function() {
  var ret = this._handle.generateSharedSecret.apply(this._handle, arguments);
  return ret ? new Buffer(ret) : ret;
};


exports.open = function(args) {
  return new EmiSocket(args);
};

exports.openMediator = function(args) {
  return new EmiP2PSocket(args);
};

exports.channelQualifier = function(type, number) {
  number = number || 0;

  var typeIsNumber = (Object.prototype.toString.call(type) == '[object Number]');
  
  if (!typeIsNumber || type < 0 || type > 3) {
    throw new Error("Invalid channel type "+type);
  }
  if (!typeIsNumber || number < 0 || number > 31) {
    throw new Error("Invalid channel number "+number);
  }
  
  return number | (type << 6);
};

exports.channelQualifierType = function(cq) {
  return (cq & 0xc0) >> 6;
};

exports.isValidChannelQualifier = function(cq) {
  return 0 == (cq & 0x20);
};

for (var key in EmiNetAddon.enums) {
  exports[key] = EmiNetAddon.enums[key];
}
