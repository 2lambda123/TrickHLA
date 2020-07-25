/*!
@file TrickHLA/OpaqueBuffer.cpp
@ingroup TrickHLA
@brief This class provides a generic opaque buffer that is in the Trick managed
memory space.

@details This is useful for sending a fixed record of data that includes byte
padding to ensure a byte alignment.

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
@trick_link_dependency{OpaqueBuffer.cpp}
@trick_link_dependency{Utilities.cpp}

@revs_title
@revs_begin
@rev_entry{Dan Dexter, NASA ER7, TrickHLA, July 2009, --, Initial implementation.}
@rev_entry{Dan Dexter, NASA ER7, TrickHLA, March 2019, --, Version 2 origin.}
@rev_entry{Edwin Z. Crues, NASA ER7, TrickHLA, June 2019, --, Version 3 rewrite.}
@revs_end

*/

// System include files.
#include <sstream>
#include <string.h>

// Trick include files.
#include "trick/exec_proto.h"
#include "trick/memorymanager_c_intf.h"
#include "trick/message_proto.h"
#include "trick/trick_byteswap.h"

// TrickHLA model include files.
#include "TrickHLA/OpaqueBuffer.hh"
#include "TrickHLA/Utilities.hh"

using namespace std;
using namespace TrickHLA;

/*!
 * @job_class{initialization}
 */
OpaqueBuffer::OpaqueBuffer()
   : alignment( 1 ),
     push_pos( 0 ),
     pull_pos( 0 ),
     capacity( 0 ),
     buffer( NULL )
{
   // Default to a buffer capacity of 1 for now just to make sure we have
   // a buffer allocated in Trick managed memory. This allows the buffer to
   // be specified in the input file as a sim variable to use with TrickHLA.
   ensure_buffer_capacity( 1 );
}

/*!
 * @details Frees allocated memory.
 * @job_class{shutdown}
 */
OpaqueBuffer::~OpaqueBuffer() // RETURN: -- None.
{
   if ( buffer != NULL ) {
      if ( TMM_is_alloced( (char *)buffer ) ) {
         TMM_delete_var_a( buffer );
      }
      buffer   = NULL;
      capacity = 0;
      push_pos = 0;
      pull_pos = 0;
   }
}

/*!
 * @job_class{initialization}
 */
void OpaqueBuffer::set_byte_alignment( unsigned int size )
{
   switch ( size ) {
      case 1:
      case 2:
      case 4:
      case 8:
      case 16:
         this->alignment = size;
         break;

      default:
         ostringstream errmsg;
         errmsg << "OpaqueBuffer::set_byte_alignment():" << __LINE__
                << " ERROR: Unsupported byte alignment: " << size << "!"
                << THLA_ENDL;
         send_hs( stderr, (char *)errmsg.str().c_str() );
         exec_terminate( __FILE__, (char *)errmsg.str().c_str() );
         break;
   }
}

/*!
 * @job_class{initialization}
 */
void OpaqueBuffer::ensure_buffer_capacity(
   size_t size )
{
   // Make sure the requested capacity is a multiple of the byte alignment.
   if ( alignment > 1 ) {
      size_t pad_size = size % alignment;
      if ( pad_size > 0 ) {
         size += pad_size;
      }
   }

   if ( size > capacity ) {
      capacity = size;
      if ( buffer == NULL ) {
         buffer = (unsigned char *)TMM_declare_var_1d( "unsigned char", (int)capacity );
      } else {
         buffer = (unsigned char *)TMM_resize_array_1d_a( buffer, (int)capacity );
      }
   } else if ( buffer == NULL ) {
      // Handle the case where the buffer has not been created yet and we
      // might have an invalid user capacity specified.

      // Make sure the capacity is at least the same size as the byte alignment.
      capacity = ( size >= alignment ) ? size : alignment;
      buffer   = (unsigned char *)TMM_declare_var_1d( "unsigned char", (int)capacity );
   }

   if ( buffer == NULL ) {
      ostringstream errmsg;
      errmsg << "OpaqueBuffer::ensure_buffer_capacity():" << __LINE__
             << " ERROR: Could not allocate memory for buffer for requested"
             << " capacity " << capacity << "!" << THLA_ENDL;
      send_hs( stderr, (char *)errmsg.str().c_str() );
      exec_terminate( __FILE__, (char *)errmsg.str().c_str() );
   }
}

void OpaqueBuffer::push_to_buffer(
   void *       src,
   size_t       size,
   EncodingEnum encoding )
{
   if ( size == 0 ) {
      send_hs( stderr, "OpaqueBuffer::push_to_buffer():%d WARNING: Unexpected zero number of bytes to push into buffer!%c",
               __LINE__, THLA_NEWLINE );
      return;
   }

   // Determine if we need to add pad bytes to achieve the desired
   // byte alignment.
   if ( alignment > 1 ) {
      size_t pad_size = push_pos % alignment;
      if ( pad_size > 0 ) {
         push_pad_to_buffer( pad_size );
      }
   }

   // Determine if we are overflowing the capacity of the buffer.
   if ( ( push_pos + size ) > capacity ) {
      ostringstream errmsg;
      errmsg << "OpaqueBuffer::push_to_buffer():" << __LINE__
             << " WARNING: Trying to push " << size << " bytes into the buffer at"
             << " position " << push_pos << ", which exceeds the buffer capacity"
             << " by " << ( ( push_pos + size ) - capacity ) << " bytes! Resizing the"
             << " buffer to accommodate the data." << THLA_ENDL;
      send_hs( stderr, (char *)errmsg.str().c_str() );
      ensure_buffer_capacity( push_pos + size );
   }

   // Display a warning if an unsupported encoding is used.
   if ( ( encoding != ENCODING_LITTLE_ENDIAN ) && ( encoding != ENCODING_BIG_ENDIAN ) && ( encoding != ENCODING_NO_ENCODING ) ) {
      ostringstream errmsg;
      errmsg << "OpaqueBuffer::push_to_buffer():" << __LINE__
             << " WARNING: Unsupported 'encoding' " << encoding << ". It must be"
             << " one of ENCODING_LITTLE_ENDIAN:" << ENCODING_LITTLE_ENDIAN
             << ", ENCODING_BIG_ENDIAN:" << ENCODING_BIG_ENDIAN
             << ", or ENCODING_NO_ENCODING:" << ENCODING_NO_ENCODING << THLA_ENDL;
      send_hs( stderr, (char *)errmsg.str().c_str() );
   }

   // Copy the source data into the buffer and do a byte-swap if needed.
   byteswap_buffer_copy( &buffer[push_pos], src, size, encoding );

   // Update buffer position where we push new data to next.
   push_pos += size;
}

/*!
 * \par<b>Assumptions and Limitations:</b>
 * - The destination must be large enough to hold size bytes of data.
 */
void OpaqueBuffer::pull_from_buffer(
   void *       dest,
   size_t       size,
   EncodingEnum encoding )
{
   if ( size == 0 ) {
      send_hs( stderr, "OpaqueBuffer::pull_from_buffer():%d WARNING: Unexpected zero number of bytes to pull from buffer!%c",
               __LINE__, THLA_NEWLINE );
      return;
   }

   // Determine if we need to remove pad bytes which were added to achieve the
   // desired byte alignment.
   if ( alignment > 1 ) {
      size_t pad_size = pull_pos % alignment;
      if ( pad_size > 0 ) {
         pull_pad_from_buffer( pad_size );
      }
   }

   // Determine if we are overflowing the capacity of the buffer.
   if ( ( pull_pos + size ) > capacity ) {
      ostringstream errmsg;
      errmsg << "OpaqueBuffer::pull_from_buffer():" << __LINE__
             << " ERROR: Trying to pull " << size << " bytes from the buffer at"
             << " position " << pull_pos << ", which exceeds the end of the buffer"
             << " by " << ( ( pull_pos + size ) - capacity ) << " bytes!" << THLA_ENDL;
      send_hs( stderr, (char *)errmsg.str().c_str() );
      exec_terminate( __FILE__, (char *)errmsg.str().c_str() );
   }

   // Display a warning if an unsupported encoding is used.
   if ( ( encoding != ENCODING_LITTLE_ENDIAN ) && ( encoding != ENCODING_BIG_ENDIAN ) && ( encoding != ENCODING_NO_ENCODING ) ) {
      ostringstream errmsg;
      errmsg << "OpaqueBuffer::pull_from_buffer():" << __LINE__
             << " WARNING: Unsupported 'encoding' " << encoding << ". It must be"
             << " one of ENCODING_LITTLE_ENDIAN:" << ENCODING_LITTLE_ENDIAN
             << ", ENCODING_BIG_ENDIAN:" << ENCODING_BIG_ENDIAN
             << ", or ENCODING_NO_ENCODING:" << ENCODING_NO_ENCODING << THLA_ENDL;
      send_hs( stderr, (char *)errmsg.str().c_str() );
   }

   // Copy the data from the buffer into the destination and do a byte-swap
   // if needed.
   byteswap_buffer_copy( dest, &buffer[pull_pos], size, encoding );

   // Update the buffer position where we pull data from.
   pull_pos += size;
}

void OpaqueBuffer::push_pad_to_buffer(
   size_t pad_size )
{
   if ( pad_size == 0 ) {
      return;
   }

   // Determine if we are overflowing the capacity of the buffer.
   if ( ( push_pos + pad_size ) > capacity ) {
      ostringstream errmsg;
      errmsg << "OpaqueBuffer::push_pad_to_buffer():" << __LINE__
             << " WARNING: Trying to push " << pad_size << " pad bytes into the"
             << " buffer at position " << push_pos << ", which exceeds the buffer"
             << " capacity by " << ( ( push_pos + pad_size ) - capacity ) << " bytes!"
             << " Resizing the buffer to accommodate the data." << THLA_ENDL;
      send_hs( stderr, (char *)errmsg.str().c_str() );
      ensure_buffer_capacity( push_pos + pad_size );
   }

   // Add the pad bytes to the buffer.
   memset( &buffer[push_pos], '\0', pad_size );

   // Update buffer position where we push new data to next.
   push_pos += pad_size;
}

void OpaqueBuffer::pull_pad_from_buffer(
   size_t pad_size )
{
   if ( pad_size == 0 ) {
      return;
   }

   // Determine if we are overflowing the capacity of the buffer.
   if ( ( pull_pos + pad_size ) > capacity ) {
      ostringstream errmsg;
      errmsg << "OpaqueBuffer::pull_pad_from_buffer():" << __LINE__
             << " ERROR: Trying to pull " << pad_size << " pad bytes from the"
             << " buffer at position " << pull_pos << ", which exceeds the end of"
             << " the buffer by " << ( ( pull_pos + pad_size ) - capacity )
             << " bytes!" << THLA_ENDL;
      send_hs( stderr, (char *)errmsg.str().c_str() );
      exec_terminate( __FILE__, (char *)errmsg.str().c_str() );
   }

   // Update the buffer position where we pull data from.
   pull_pos += pad_size;
}

/*!
 * \par<b>Assumptions and Limitations:</b>
 * - The destination must be large enough to hold size bytes of data.
 */
void OpaqueBuffer::byteswap_buffer_copy(
   void *       dest,
   void *       src,
   size_t       size,
   EncodingEnum encoding )
{
   // Determine if we need to byteswap the data.
   if ( Utilities::is_transmission_byteswap( encoding ) ) {

      // Do a byte-swap based on the size of the data.
      switch ( size ) {
         case 1: {
            memcpy( dest, src, 1 );
            break;
         }
         case 2: {
            unsigned short *us_src  = (unsigned short *)src;
            unsigned short *us_dest = (unsigned short *)dest;
            us_dest[0]              = Utilities::byteswap_unsigned_short( us_src[0] );
            break;
         }
         case 4: {
            unsigned int *ui_src  = (unsigned int *)src;
            unsigned int *ui_dest = (unsigned int *)dest;
            ui_dest[0]            = Utilities::byteswap_unsigned_int( ui_src[0] );
            break;
         }
         case 8: {
            // The "unsigned long long" type is at least 64 bits.
            if ( size == sizeof( unsigned long long ) ) {
               unsigned long long *ull_src  = (unsigned long long *)src;
               unsigned long long *ull_dest = (unsigned long long *)dest;
               ull_dest[0]                  = Utilities::byteswap_unsigned_long_long( ull_src[0] );
            } else {
               // Try doing the byteswap as a 64 bit double as a last resort if
               // the unsigned long long on this computer is not 64 bits.
               double *d_src  = (double *)src;
               double *d_dest = (double *)dest;
               d_dest[0]      = Utilities::byteswap_double( d_src[0] );
            }
            break;
         }
         default: {
            // The "unsigned long long" type is at least 64 bits.
            if ( size == sizeof( unsigned long long ) ) {
               unsigned long long *ull_src  = (unsigned long long *)src;
               unsigned long long *ull_dest = (unsigned long long *)dest;
               ull_dest[0]                  = Utilities::byteswap_unsigned_long_long( ull_src[0] );
            } else {
               ostringstream errmsg;
               errmsg << "OpaqueBuffer::byteswap_buffer_copy():"
                      << __LINE__ << " ERROR: Don't know how to byteswap "
                      << size << " bytes!" << THLA_ENDL;
               send_hs( stderr, (char *)errmsg.str().c_str() );
               exec_terminate( __FILE__, (char *)errmsg.str().c_str() );
            }
            break;
         }
      }
   } else {
      // No byte-swap needed so just copy the data.
      memcpy( dest, src, size );
   }
}
