// @file deferredinvoker.h

/**
 *    Copyright (C) 2008 10gen Inc.
 *
 *    This program is free software: you can redistribute it and/or  modify
 *    it under the terms of the GNU Affero General Public License, version 3,
 *    as published by the Free Software Foundation.
 *
 *    This program is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU Affero General Public License for more details.
 *
 *    You should have received a copy of the GNU Affero General Public License
 *    along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#pragma once

#include "mongomutex.h"

namespace mongo {

    /** defer work for invocation by another thread.  presumption is that thread is outside of locks 
        more than the source thread that queues the deferred invocations.

        this class is in db/ as it is dbMutex (mongomutex) specific (so far).

        using boost::bind() would be more elegant, but this will be used in a very hot code path, so 
        we need to test performance impact before doing that.

        using a functor instead of go() might be more elegant too, once again, would like to test any 
        performance differential.

        V - copyable object we can queue
            V must have a static method go(V) or go(const V&)

        see DefInvoke in dbtests/ for an example.
    */
    template< class V >
    class DeferredInvoker {
    public:
        DeferredInvoker() {
            _which = 0;
        }

        void defer(V v) { 
            // only one writer allowed.  however the invoke processing below can occur concurrently with 
            // writes (for the most part)
            DEV dbMutex.assertWriteLocked(); 

            _queues[_which].push_back(v);
        }

        /** call to process pending invocations.  

            concurrency: only one thread at a time may call invoke().  however other threads can call defer() 
                         simultaneously. 
            
            normally, you call this outside of any lock.  but if you want to fully drain the queue,
            call from within a read lock.  a good way to drain :
            {
              // drain with minimal time in lock
              d.invoke();
              readlock lk;
              d.invoke();
              ...
            }
        */
        void invoke() { 
            int full = _which;
            int empty = _which ^ 1;
           
            // flip
            {
                readlock lk;
                assert( _queues[empty].empty() ); // queue-ers only touch the active queue, not the other one
                _which = empty;
            }
            _drain( _queues[full] );
        }

    private:
        int _which; // 0 or 1
        typedef vector<V> Queue;
        Queue _queues[2];

        void _drain(Queue& queue) {
            unsigned oldCap = queue.capacity();
            for( Queue::iterator i = queue.begin(); i != queue.end(); i++ ) { 
                V::go(*i);
            }
            queue.clear();
            DEV assert( queue.capacity() == oldCap ); // just checking that clear() doesn't deallocate, we don't want that            
        }
    };

}
