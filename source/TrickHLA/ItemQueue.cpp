/*!
@file TrickHLA/ItemQueue.cpp
@ingroup TrickHLA
@brief This class represents a queue for holding Items.

@copyright Copyright 2019 United States Government as represented by the
Administrator of the National Aeronautics and Space Administration.
No copyright is claimed in the United States under Title 17, U.S. Code.
All Other Rights Reserved.

\par<b>Responsible Organization</b>
Simulation and Graphics Branch, Mail Code ER7\n
Software, Robotics & Simulation Division\n
NASA, Johnson Space Center\n
2101 NASA Parkway, Houston, TX  77058

@tldh
@trick_link_dependency{Item.cpp}
@trick_link_dependency{ItemQueue.cpp}

@revs_title
@revs_begin
@rev_entry{Dan Dexter, NASA/ER7, IMSim, Feb 2009, --, Initial implementation.}
@rev_entry{Dan Dexter, NASA ER7, TrickHLA, March 2019, --, Version 2 origin.}
@rev_entry{Edwin Z. Crues, NASA ER7, TrickHLA, March 2019, --, Version 3 rewrite.}
@revs_end

*/

// System include files.
#include <pthread.h>
#include <string>

// Trick include files.
#include "trick/message_proto.h"

// TrickHLA include files.
#include "TrickHLA/ItemQueue.hh"
#include "TrickHLA/Utilities.hh"

//using namespace std;
using namespace TrickHLA;

/*!
 * @job_class{initialization}
 */
ItemQueue::ItemQueue()
   : count( 0 ),
     head( NULL ),
     tail( NULL ),
     original_head( NULL )
{
   // Initialize the mutex.
   pthread_mutex_init( &mutex, NULL );
}

/*!
 * @job_class{shutdown}
 */
ItemQueue::~ItemQueue()
{
   // Empty the queue by popping items off of it.
   while ( !empty() ) {
      pop();
   }

   // Make sure we destroy the mutex.
   pthread_mutex_destroy( &mutex );
}

/*!
 * @job_class{initialization}
 */
void ItemQueue::dump_head_pointers(
   const char *name )
{
   // Note: this routine does not lock the data so it must be called after
   //       lock() routine has been called...

   Item *temp = head->next;

   send_hs( stdout,
            "ItemQueue::dump_head_pointers(%s):%d Current element is %p %c",
            name, __LINE__, head, THLA_NEWLINE );

   // adjust to the next item off the stack in a thread-safe way.
   while ( temp != NULL ) { // while there are any more elements
      send_hs( stdout,
               "ItemQueue::dump_head_pointers(%s):%d Current element points to %p %c",
               name, __LINE__, temp, THLA_NEWLINE );

      // Adjust the "head" to point to the next item in the linked-list.
      temp = temp->next;
   }
}

/*!
 * @job_class{initialization}
 */
void ItemQueue::next(
   Item *item )
{
   // Note: this routine does not lock the data so it must be called after
   //       lock() routine has been called...

   // adjust to the next item off the stack in a thread-safe way.
   if ( item->next != NULL ) { // is this not the end of the queue

      // if this is the first call to the routine, capture the head pointer so
      // it can be restored once we are done with walking the queue...
      if ( original_head == NULL ) {
         original_head = head;
      }

      // Adjust the "head" to point to the next item in the linked-list.
      head = item->next;
   }
}

/*!
 * @job_class{initialization}
 */
void ItemQueue::pop()
{
   // Pop the item off the stack in a thread-safe way.
   lock();
   if ( !empty() ) {
      Item *item = head;

      // Adjust the "head" to point to the next item in the linked-list.
      if ( head == tail ) {
         head = NULL;
         tail = NULL;
      } else {
         head = item->next;
      }

      // Make sure we delete the Item we created when we pushed it on the queue.
      delete item;

      count--;
   }
   unlock();
}

/*!
 * @job_class{initialization}
 */
void ItemQueue::push( // RETURN: -- None.
   Item *item )       // IN: -- Item to put into the queue.
{
   // Add the item to the queue in a thread-safe way.
   lock();

   // Add the item to the tail-end of the linked list.
   if ( tail == NULL ) {
      head = item;
      tail = item;
   } else {
      tail->next = item;
      tail       = item;
   }

   count++;

   unlock();
}

/*!
 * @job_class{initialization}
 */
void ItemQueue::rewind()
{
   // Note: this routine does not lock the data so it must be called after
   //       lock() routine has been called...

   // if the next() routine was ever executed to walk thru the queue without
   // popping, restore the queue's original 'head' pointer.
   if ( original_head != NULL ) {
      head          = original_head;
      original_head = NULL;
   }
}