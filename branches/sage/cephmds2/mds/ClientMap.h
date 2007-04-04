// -*- mode:C++; tab-width:8; c-basic-offset:2; indent-tabs-mode:t -*- 
/*
 * Ceph - scalable distributed file system
 *
 * Copyright (C) 2004-2006 Sage Weil <sage@newdream.net>
 *
 * This is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License version 2.1, as published by the Free Software 
 * Foundation.  See file COPYING.
 * 
 */

#ifndef __CLIENTMAP_H
#define __CLIENTMAP_H

#include "msg/Message.h"

#include <set>
using namespace std;

#include <ext/hash_map>
using namespace __gnu_cxx;


/*
 * this structure is used by the MDS purely so that
 * it can remember client addresses (entity_inst_t)
 * while processing request(s) on behalf of clients.
 * as such it's only really a sort of short-term cache.
 * 
 * it also remembers which clients mounted via this MDS,
 * for the same reason (so that mounted clients can be 
 * contacted if necessary).
 */
class ClientMap {
private:
  version_t version;
  version_t projected;
  version_t committing;
  version_t committed;
  map<version_t, list<Context*> > commit_waiters;

public:
  ClientMap() : version(0), projected(0), committing(0), committed(0) {}

  version_t get_version() { return version; }
  version_t get_projected() { return projected; }
  version_t get_committing() { return committing; }
  version_t get_committed() { return committed; }

  version_t inc_projected() { return ++projected; }
  void reset_projected() { projected = version; }
  void set_committing(version_t v) { committing = v; }
  void set_committed(version_t v) { committed = v; }

  void add_commit_waiter(Context *c) { 
    commit_waiters[committing].push_back(c); 
  }
  void take_commit_waiters(version_t v, list<Context*>& ls) { 
    ls.swap(commit_waiters[v]);
    commit_waiters.erase(v);
  }

  // client mount, inst info
private:
  hash_map<int,entity_inst_t> client_inst;
  set<int>           client_mount;
  hash_map<int, int> client_ref;

  void inc_ref(int client, const entity_inst_t& inst) {
    if (client_inst.count(client)) {
      assert(client_inst[client] == inst);
      assert(client_ref.count(client));
    } else {
      client_inst[client] = inst;
    }
    client_ref[client]++;
  }
  void dec_ref(int client) {
    assert(client_ref.count(client));
    assert(client_ref[client] > 0);
    client_ref[client]--;
    if (client_ref[client] == 0) {
      client_ref.erase(client);
      client_inst.erase(client);
    }
  }

public:
  bool empty() {
    return client_inst.empty() && client_mount.empty() && client_ref.empty();
  }

  const entity_inst_t& get_inst(int client) {
    assert(client_inst.count(client));
    return client_inst[client];
  }
  const set<int>& get_mount_set() { return client_mount; }
  
  void add_mount(const entity_inst_t& inst) {
    inc_ref(inst.name.num(), inst);
    client_mount.insert(inst.name.num());
    version++;
  }
  void rem_mount(int client) {
    dec_ref(client);
    client_mount.erase(client);
    version++;
  }
  
  
  void add_open(int client, const entity_inst_t& inst) {
    inc_ref(client, inst);
    //version++;
  }
  void dec_open(int client) {
    dec_ref(client);
    //version++;
  }


private:
  // -- completed requests --
  // client id -> tid -> result code
  map<int, set<tid_t> > completed_requests;  // completed client requests
  map<int, map<tid_t, Context*> > waiting_for_trim;
 
public:
  void add_completed_request(metareqid_t ri) {
    completed_requests[ri.client].insert(ri.tid);
  }
  void trim_completed_requests(int client, 
			       tid_t mintid) {  // zero means trim all!
    map<int, set<tid_t> >::iterator p = completed_requests.find(client);
    if (p == completed_requests.end())
      return;

    // trim
    while (!p->second.empty() && (mintid == 0 || *p->second.begin() < mintid))
      p->second.erase(p->second.begin());
    if (p->second.empty())
      completed_requests.erase(p);

    // kick waiters
    map<int, map<tid_t,Context*> >::iterator q = waiting_for_trim.find(client);
    if (q != waiting_for_trim.end()) {
      list<Context*> fls;
      while (q->second.begin()->first < mintid) {
	fls.push_back(q->second.begin()->second);
	q->second.erase(q->second.begin());
      }
      if (q->second.empty())
	waiting_for_trim.erase(q);
      finish_contexts(fls);
    }
  }
  void add_trim_waiter(metareqid_t ri, Context *c) {
    waiting_for_trim[ri.client][ri.tid] = c;
  }
  bool have_completed_request(metareqid_t ri) {
    return completed_requests.count(ri.client) &&
      completed_requests[ri.client].count(ri.tid);
  }


  // -- encoding --
  void encode(bufferlist& bl) {
    bl.append((char*)&version, sizeof(version));
    ::_encode(client_inst, bl);
    ::_encode(client_mount, bl);
    ::_encode(client_ref, bl);
  }
  void decode(bufferlist& bl, int& off) {
    bl.copy(off, sizeof(version), (char*)&version);
    off += sizeof(version);
    ::_decode(client_inst, bl, off);
    ::_decode(client_mount, bl, off);
    ::_decode(client_ref, bl, off);

    projected = committing = committed = version;
  }
};

#endif
