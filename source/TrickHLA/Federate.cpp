/*!
@file TrickHLA/Federate.cpp
@ingroup TrickHLA
@brief This class is the abstract base class for representing timelines.

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
@trick_link_dependency{Int64Time.cpp}
@trick_link_dependency{Timeline.cpp}
@trick_link_dependency{SimTimeline.cpp}
@trick_link_dependency{ScenarioTimeline.cpp}
@trick_link_dependency{CTETimelineBase.cpp}
@trick_link_dependency{FedAmb.cpp}
@trick_link_dependency{Federate.cpp}
@trick_link_dependency{Manager.cpp}
@trick_link_dependency{ExecutionControlBase.cpp}

@revs_title
@revs_begin
@rev_entry{Edwin Z. Crues, Titan Systems Corp., DIS, Titan Systems Corp., --, Initial investigation.}
@rev_entry{Dan Dexter, NASA ER7, TrickHLA, March 2019, --, Version 2 origin.}
@rev_entry{Edwin Z. Crues, NASA ER7, TrickHLA, Jan 2019, --, SRFOM support & test.}
@rev_entry{Edwin Z. Crues, NASA ER7, TrickHLA, March 2019, --, Version 3 rewrite.}
@revs_end

*/

// System include files.
#include <arpa/inet.h>
#include <cstddef>
#include <cstdio>
#include <cstdlib> // for atof
#include <float.h>
#include <fstream> // for ifstream
#include <iostream>
#include <limits>
#include <memory> // for auto_ptr
#include <sstream>
#include <stdint.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h> // for sleep()

// Trick include files.
#include "trick/Clock.hh"
#include "trick/Executive.hh"
#include "trick/exec_proto.h"

#include "trick/CheckPointRestart.hh"
#include "trick/CheckPointRestart_c_intf.hh"
#include "trick/DataRecordDispatcher.hh" //DANNY2.7 need the_drd to init data recording groups when restoring at init time (IMSIM)
#include "trick/command_line_protos.h"
#include "trick/input_processor_proto.h"
#include "trick/memorymanager_c_intf.h"
#include "trick/message_proto.h"
#include "trick/release.h"

// TrickHLA include files.
#include "TrickHLA/CompileConfig.hh"
#include "TrickHLA/ExecutionConfigurationBase.hh"
#include "TrickHLA/ExecutionControlBase.hh"
#include "TrickHLA/FedAmb.hh"
#include "TrickHLA/Federate.hh"
#include "TrickHLA/Int64Interval.hh"
#include "TrickHLA/Manager.hh"
#include "TrickHLA/OwnershipItem.hh"
#include "TrickHLA/ScenarioTimeline.hh"
#include "TrickHLA/SimTimeline.hh"
#include "TrickHLA/StringUtilities.hh"
#include "TrickHLA/TimeOfDayTimeline.hh"
#include "TrickHLA/Types.hh"
#include "TrickHLA/Utilities.hh"

// Access the Trick global objects for CheckPoint restart and the Clock.
extern Trick::CheckPointRestart *the_cpr;

// Note: This has to follow the Federate include.
#include <RTI/RTIambassadorFactory.h>

#ifdef __cplusplus
extern "C" {
#endif
// C based model includes.

extern pthread_mutex_t ip_mutex;

#ifdef __cplusplus
}
#endif

using namespace std;
using namespace RTI1516_NAMESPACE;
using namespace TrickHLA;

/*!
 * @details NOTE: In most cases, we would allocate and set default names in
 * the constructor. However, since we want this class to be Input Processor
 * friendly, we cannot do that here since the Input Processor may not have
 * been initialized yet. So, we have to set the name information to NULL and
 * then allocate and set the defaults in the initialization job if not
 * already set in the input stream.
 *
 * @job_class{initialization}
 */
Federate::Federate()
   : name( NULL ),
     type( NULL ),
     federation_name( NULL ),
     local_settings( NULL ),
     FOM_modules( NULL ),
     MIM_module( NULL ),
     lookahead_time( 0.0 ),
     time_regulating( true ),
     time_constrained( true ),
     time_management( true ),
     enable_known_feds( true ),
     known_feds_count( 0 ),
     known_feds( NULL ),
     can_rejoin_federation( false ),
     freeze_delay_frames( 2 ),
     unfreeze_after_save( false ),
     federation_created_by_federate( false ),
     federation_exists( false ),
     federation_joined( false ),
     all_federates_joined( false ),
     lookahead( 1.0 ),
     shutdown_called( false ),
     HLA_save_directory( NULL ),
     initiate_save_flag( false ),
     restore_process( No_Restore ),
     prev_restore_process( No_Restore ),
     initiate_restore_flag( false ),
     restore_in_progress( false ),
     restore_failed( false ),
     restore_is_imminent( false ),
     announce_save( false ),
     save_label_generated( false ),
     save_request_complete( false ),
     save_completed( false ),
     stale_data_counter( 0 ),
     announce_restore( false ),
     restore_label_generated( false ),
     restore_begun( false ),
     restore_request_complete( false ),
     restore_completed( false ),
     federation_restore_failed_callback_complete( false ),
     federate_has_been_restarted( false ),
     publish_data( true ),
     running_feds_count( 0 ),
     running_feds( NULL ),
     running_feds_count_at_time_of_restore( 0 ),
     checkpoint_rt_itimer( Off ),
     announce_freeze( false ),
     freeze_the_federation( false ),
     execution_has_begun( false ),
     time_adv_grant( false ),
     granted_time( 0.0 ),
     requested_time( 0.0 ),
     HLA_time( 0.0 ),
     start_to_save( false ),
     start_to_restore( false ),
     restart_flag( false ),
     restart_cfg_flag( false ),
     time_regulating_state( false ),
     time_constrained_state( false ),
     got_startup_sp( false ),
     make_copy_of_run_directory( false ),
     MOM_HLAfederation_class_handle(),
     MOM_HLAfederatesInFederation_handle(),
     MOM_HLAautoProvide_handle(),
     mom_HLAfederation_instance_name_map(),
     auto_provide_setting( -1 ),
     orig_auto_provide_setting( -1 ),
     MOM_HLAfederate_class_handle(),
     MOM_HLAfederateType_handle(),
     MOM_HLAfederateName_handle(),
     MOM_HLAfederate_handle(),
     mom_HLAfederate_inst_name_map(),
     joined_federate_name_map(),
     joined_federate_handles(),
     joined_federate_names(),
     RTI_ambassador( NULL ),
     federate_ambassador( NULL ),
     manager( NULL ),
     execution_control( NULL )
{
   TRICKHLA_INIT_FPU_CONTROL_WORD;

   cstr_save_label[0]      = '\0';
   cstr_restore_label[0]   = '\0';
   checkpoint_file_name[0] = '\0';

   // As a sanity check validate the FPU code word.
   TRICKHLA_VALIDATE_FPU_CONTROL_WORD;
}

/*!
 * @details Free up the Trick allocated memory associated with the attributes
 * of this class.
 * @job_class{shutdown}
 */
Federate::~Federate()
{
   // Shutdown the federate and try to destroy the federation.
   if ( execution_has_begun ) {
      // FIXME: Not sure that this is a good idea.
      shutdown();
   }

   // Free the memory used for the federate name.
   if ( name != static_cast< char * >( NULL ) ) {
      if ( TMM_is_alloced( name ) ) {
         TMM_delete_var_a( name );
      }
      name = static_cast< char * >( NULL );
   }

   // Free the memory used for the federate type.
   if ( type != static_cast< char * >( NULL ) ) {
      if ( TMM_is_alloced( type ) ) {
         TMM_delete_var_a( type );
      }
      type = static_cast< char * >( NULL );
   }

   // Free the memory used for local-settings
   if ( local_settings != static_cast< char * >( NULL ) ) {
      if ( TMM_is_alloced( local_settings ) ) {
         TMM_delete_var_a( local_settings );
      }
      local_settings = static_cast< char * >( NULL );
   }

   // Free the memory used for the Federation Execution name.
   if ( federation_name != static_cast< char * >( NULL ) ) {
      if ( TMM_is_alloced( federation_name ) ) {
         TMM_delete_var_a( federation_name );
      }
      federation_name = static_cast< char * >( NULL );
   }

   // Free the memory used by the FOM module filenames.
   if ( FOM_modules != static_cast< char * >( NULL ) ) {
      if ( TMM_is_alloced( FOM_modules ) ) {
         TMM_delete_var_a( FOM_modules );
      }
      FOM_modules = static_cast< char * >( NULL );
   }
   if ( MIM_module != static_cast< char * >( NULL ) ) {
      if ( TMM_is_alloced( MIM_module ) ) {
         TMM_delete_var_a( MIM_module );
      }
      MIM_module = static_cast< char * >( NULL );
   }

   // Free the memory used by the array of known Federates for the Federation.
   if ( known_feds != static_cast< KnownFederate * >( NULL ) ) {
      for ( int i = 0; i < known_feds_count; i++ ) {
         if ( known_feds[i].MOM_instance_name != static_cast< char * >( NULL ) ) {
            if ( TMM_is_alloced( known_feds[i].MOM_instance_name ) ) {
               TMM_delete_var_a( known_feds[i].MOM_instance_name );
            }
            known_feds[i].MOM_instance_name = static_cast< char * >( NULL );
         }
         if ( known_feds[i].name != static_cast< char * >( NULL ) ) {
            if ( TMM_is_alloced( known_feds[i].name ) ) {
               TMM_delete_var_a( known_feds[i].name );
            }
            known_feds[i].name = static_cast< char * >( NULL );
         }
      }
      TMM_delete_var_a( known_feds );
      known_feds       = static_cast< KnownFederate * >( NULL );
      known_feds_count = 0;
   }

   // Clear the joined federate name map.
   joined_federate_name_map.clear();

   // Clear the set of federate handles for the joined federates.
   joined_federate_handles.clear();

   // Clear the list of joined federate names.
   joined_federate_names.clear();

   // Free the memory used by the array of running Federates for the Federation.
   clear_running_feds();

   // Clear the MOM HLAfederation instance name map.
   mom_HLAfederation_instance_name_map.clear();

   // Clear the list of discovered object federate names.
   mom_HLAfederate_inst_name_map.clear();

   // Set the references to the ambassadors.
   federate_ambassador = static_cast< FedAmb * >( NULL );
}

/*!
 * @job_class{initialization}
 */
void Federate::print_version() const
{
   if ( should_print( DEBUG_LEVEL_1_TRACE, DEBUG_SOURCE_FEDERATE ) ) {
      string rti_name, rti_version;

#if defined( UNSUPPORTED_RTI_NAME_API )
      rti_name = RTI_NAME;
#else
      StringUtilities::to_string( rti_name, RTI1516_NAMESPACE::rtiName() );
#endif

#if defined( UNSUPPORTED_RTI_VERSION_API )
      rti_version = RTI_VERSION;
#else
      StringUtilities::to_string( rti_version, RTI1516_NAMESPACE::rtiVersion() );
#endif

      send_hs( stdout, "Manager::print_version():%d TrickHLA-version:'%s', TrickHLA-release-date:'%s', RTI-name:'%s', RTI-version:'%s'%c",
               __LINE__, Utilities::get_version().c_str(),
               Utilities::get_release_date().c_str(),
               rti_name.c_str(), rti_version.c_str(), THLA_NEWLINE );
   }
}

bool Federate::should_print(
   const DebugLevelEnum & level,
   const DebugSourceEnum &code ) const
{
   if ( federate_ambassador != NULL ) {
      return federate_ambassador->should_print( level, code );
   }
   return true;
}

/*!
 * @details Check that the FPU Control Word matches the value at simulation
 *  startup.  If not it will reset it back to the startup value.  It will use
 *  the FPU Control Word value set by the Python Input Processor.
 */
void Federate::fix_FPU_control_word()
{
#if defined( FPU_CW_PROTECTION ) && ( defined( __i386__ ) || defined( __x86_64__ ) )
   // Get the current FPU control word value.
   TRICKHLA_SAVE_FPU_CONTROL_WORD;

   // Reset the FPU control word value at program startup to use the current
   // FPU control word value that has been set by the input processor when
   // Python changed it to use IEEE-754 double precision floating point numbers
   // with a 53-bit Mantissa.
   if ( _fpu_cw != __fpu_control ) {
      // Reset the original FPU Control Word to the current value set by Python.
      __fpu_control = _fpu_cw;
   }
#endif

   TRICKHLA_VALIDATE_FPU_CONTROL_WORD;

   return;
}

/*!
 * \par<b>Assumptions and Limitations:</b>
 * - The TrickHLA::FedAmb class is actually an abstract class.  Therefore,
 * the actual object instance being passed in is an instantiable polymorphic
 * child of the RTI1516_NAMESPACE::FederateAmbassador class.
 *
 * - The TrickHLA::ExecutionControlBase class is actually an abstract class.
 * Therefore, the actual object instance being passed in is an instantiable
 * polymorphic child of the TrickHLA::ExecutionControlBase class.
 *
 * @job_class{default_data}
 */
void Federate::setup(
   FedAmb &              federate_amb,
   Manager &             federate_manager,
   ExecutionControlBase &federate_execution_control )
{

   // Set the Federate ambassador.
   this->federate_ambassador = &federate_amb;

   // Set the Federate manager.
   this->manager = &federate_manager;

   // Set the Federate execution control.
   this->execution_control = &federate_execution_control;

   // Setup the TrickHLA::FedAmb instance.
   this->federate_ambassador->setup( *this, *( this->manager ) );

   // Setup the TrickHLA::Manager instance.
   this->manager->setup( *this, *( this->execution_control ) );

   // Set up the TrickHLA::ExecutionControl instance.
   this->execution_control->setup( *this, *( this->manager ) );

   return;
}

/*!
 * \par<b>Assumptions and Limitations:</b>
 * - The TrickHLA::FedAmb class is actually an abstract class.  Therefore,
 * the actual object instance being passed in is an instantiable polymorphic
 * child of the RTI1516_NAMESPACE::FederateAmbassador class.
 * @job_class{initialization}
 */
void Federate::initialize()
{
   TRICKHLA_VALIDATE_FPU_CONTROL_WORD;

   // Make sure the federate name has been specified.
   if ( ( name == NULL ) || ( *name == '\0' ) ) {
      ostringstream errmsg;
      errmsg << "Federate::initialize():" << __LINE__
             << " Unexpected NULL federate name." << THLA_ENDL;
      send_hs( stderr, (char *)errmsg.str().c_str() );
      exec_terminate( __FILE__, (char *)errmsg.str().c_str() );
      return;
   }

   // If a federate type is not specified make it the same as the federate name.
   if ( ( type == NULL ) || ( *type == '\0' ) ) {
      type = TMM_strdup( name );
   }

   if ( should_print( DEBUG_LEVEL_2_TRACE, DEBUG_SOURCE_FEDERATE ) ) {
      send_hs( stdout, "Federate::initialize():%d Federate:\"%s\" Type:\"%s\"%c",
               __LINE__, name, type, THLA_NEWLINE );
   }

   // Check to make sure we have a reference to the TrickHLA::FedAmb.
   if ( federate_ambassador == NULL ) {
      ostringstream errmsg;
      errmsg << "Federate::initialize():" << __LINE__
             << " Unexpected NULL TrickHLA::FedAmb." << THLA_ENDL;
      send_hs( stderr, (char *)errmsg.str().c_str() );
      exec_terminate( __FILE__, (char *)errmsg.str().c_str() );
      return;
   }

   // Initialize the TrickHLA::FedAmb object instance.
   federate_ambassador->initialize();

   // Check to make sure we have a reference to the TrickHLA::Manager.
   if ( manager == NULL ) {
      ostringstream errmsg;
      errmsg << "Federate::initialize():" << __LINE__
             << " Unexpected NULL TrickHLA::Manager." << THLA_ENDL;
      send_hs( stderr, (char *)errmsg.str().c_str() );
      exec_terminate( __FILE__, (char *)errmsg.str().c_str() );
      return;
   }

   // Check to make sure we have a reference to the TrickHLA::ExecutionControlBase.
   if ( execution_control == NULL ) {
      ostringstream errmsg;
      errmsg << "Federate::initialize():" << __LINE__
             << " Unexpected NULL TrickHLA::ExecutionControlBase." << THLA_ENDL;
      send_hs( stderr, (char *)errmsg.str().c_str() );
      exec_terminate( __FILE__, (char *)errmsg.str().c_str() );
      return;
   }

   // Initialize the TrickHLA::ExecutionControl object instance.
   execution_control->initialize();

   // Finish doing the initialization.
   restart_initialization();

   TRICKHLA_VALIDATE_FPU_CONTROL_WORD;
}

/*!
 * @job_class{initialization}
 */
void Federate::restart_initialization()
{
   if ( should_print( DEBUG_LEVEL_2_TRACE, DEBUG_SOURCE_FEDERATE ) ) {
      send_hs( stdout, "Federate::restart_initialization():%d %c",
               __LINE__, THLA_NEWLINE );
   }

   TRICKHLA_VALIDATE_FPU_CONTROL_WORD;

   // Update the lookahead time in our time object.
   set_lookahead( lookahead_time );

   // Disable time management if the federate is not setup to be
   // time-regulating or time-constrained.
   if ( time_management && !time_regulating && !time_constrained ) {
      time_management = false;
   }

   if ( federate_ambassador == static_cast< FedAmb * >( NULL ) ) {
      ostringstream errmsg;
      errmsg << "Federate::restart_initialization():" << __LINE__
             << " NULL pointer to FederateAmbassador!" << THLA_ENDL;
      send_hs( stderr, (char *)errmsg.str().c_str() );
      exec_terminate( __FILE__, (char *)errmsg.str().c_str() );
   }

   // Verify the federate name.
   if ( ( name == NULL ) || ( *name == '\0' ) ) {
      ostringstream errmsg;
      errmsg << "Federate::restart_initialization():" << __LINE__
             << " NULL or zero length Federate Name." << THLA_ENDL;
      send_hs( stderr, (char *)errmsg.str().c_str() );
      exec_terminate( __FILE__, (char *)errmsg.str().c_str() );
      return;
   }

   // The lookahead time can not be negative.
   if ( lookahead_time < 0.0 ) {
      ostringstream errmsg;
      errmsg << "Federate::restart_initialization():" << __LINE__
             << " Invalid HLA lookahead time!"
             << " Lookahead time (" << lookahead_time << " seconds)"
             << " must be greater than or equal to zero and not negative. Make"
             << " sure 'lookahead_time' in your input or modified-data file is"
             << " not a negative number." << THLA_ENDL;
      send_hs( stderr, (char *)errmsg.str().c_str() );
      exec_terminate( __FILE__, (char *)errmsg.str().c_str() );
   }

   // Verify the FOM-modules value.
   if ( ( FOM_modules == NULL ) || ( *FOM_modules == '\0' ) ) {
      ostringstream errmsg;
      errmsg << "Federate::restart_initialization():" << __LINE__
             << " Invalid FOM-modules."
             << " Please check your input or modified-data files to make sure"
             << " 'FOM_modules' is correctly specified, where 'FOM_modules' is";
      errmsg << " a comma separated list of FOM-module filenames.";
      errmsg << THLA_ENDL;
      send_hs( stderr, (char *)errmsg.str().c_str() );
      exec_terminate( __FILE__, (char *)errmsg.str().c_str() );
   }

   // Verify the Federation execution name.
   if ( ( federation_name == NULL ) || ( *federation_name == '\0' ) ) {
      ostringstream errmsg;
      errmsg << "Federate::restart_initialization():" << __LINE__
             << " Invalid Federate Execution Name."
             << " Please check your input or modified-data files to make sure"
             << " the 'federation_name' is correctly specified." << THLA_ENDL;
      send_hs( stderr, (char *)errmsg.str().c_str() );
      exec_terminate( __FILE__, (char *)errmsg.str().c_str() );
   }

   // Check if there are known Federate in the Federation.
   if ( enable_known_feds ) {

      // Only need to do anything if there are known federates.
      if ( ( known_feds_count <= 0 ) || ( known_feds == static_cast< KnownFederate * >( NULL ) ) ) {

         // Make sure the count reflects the state of the array.
         known_feds_count = 0;

         // If we are enabling known federates, then there probably should be some.
         ostringstream errmsg;
         errmsg << "Federate::restart_initialization():" << __LINE__
                << " No Known Federates Specified for the Federation."
                << THLA_ENDL;
         send_hs( stderr, (char *)errmsg.str().c_str() );
         exec_terminate( __FILE__, (char *)errmsg.str().c_str() );
      }

      // Validate the name of each Federate known to be in the Federation.
      for ( int i = 0; i < known_feds_count; i++ ) {

         // A NULL or zero length Federate name is not allowed.
         if ( ( known_feds[i].name == static_cast< char * >( NULL ) ) || ( strlen( known_feds[i].name ) == 0 ) ) {
            ostringstream errmsg;
            errmsg << "Federate::restart_initialization():" << __LINE__
                   << " Invalid name of known Federate at array index: "
                   << i << THLA_ENDL;
            send_hs( stderr, (char *)errmsg.str().c_str() );
            exec_terminate( __FILE__, (char *)errmsg.str().c_str() );
         }
      }
   }
   TRICKHLA_VALIDATE_FPU_CONTROL_WORD;

   return;
}

/*!
 * @details This performs all the startup steps prior to any multi-phase
 * initialization process defined by the user.  The multi-phase initialization
 * will be performed as initialization jobs between P_INIT and P_LAST
 * phased initialization jobs.
 *
 * @job_class{initialization}
 */
void Federate::pre_multiphase_initialization()
{

   // Perform the Execution Control specific pre-multi-phase initialization.
   execution_control->pre_multi_phase_init_processes();

   // Debug printout.
   if ( should_print( DEBUG_LEVEL_1_TRACE, DEBUG_SOURCE_FEDERATE ) ) {
      send_hs( stdout, "Federate::pre_multiphase_initialization():%d\n     Completed pre-multiphase initialization...%c",
               __LINE__, THLA_NEWLINE );
   }

   // Check to make sure we have a reference to the TrickHLA::Manager.
   if ( manager == NULL ) {
      ostringstream errmsg;
      errmsg << "Federate::initialize():" << __LINE__
             << " Unexpected NULL TrickHLA::Manager." << THLA_ENDL;
      send_hs( stderr, (char *)errmsg.str().c_str() );
      exec_terminate( __FILE__, (char *)errmsg.str().c_str() );
      return;
   }

   // Initialize the TrickHLA::Manager object instance.
   manager->initialize();

   return;
}

/*!
 * @details This performs all the startup steps after any multi-phase
 * initialization process defined by the user.
 *
 * @job_class{initialization}
 */
void Federate::post_multiphase_initialization()
{

   // Perform the Execution Control specific post-multi-phase initialization.
   execution_control->post_multi_phase_init_processes();

   // Debug printout.
   if ( should_print( DEBUG_LEVEL_1_TRACE, DEBUG_SOURCE_FEDERATE ) ) {
      send_hs( stdout, "Federate::post_multiphase_initialization():%d\n     Simulation has started and is now running...%c",
               __LINE__, THLA_NEWLINE );
   }

   // Mark the federate as having begun execution.
   this->set_federate_has_begun_execution();

   return;
}

/*!
 * @job_class{initialization}
 */
void Federate::create_RTI_ambassador_and_connect()
{
   // Macro to save the FPU Control Word register value.
   TRICKHLA_SAVE_FPU_CONTROL_WORD;

   // Just return if we have already created the RTI ambassador.
   if ( RTI_ambassador.get() != NULL ) {
      // Macro to restore the saved FPU Control Word register value.
      TRICKHLA_RESTORE_FPU_CONTROL_WORD;
      TRICKHLA_VALIDATE_FPU_CONTROL_WORD;
      return;
   }

   // To work around an issue caused by the Java VM throwing a Signal Floating
   // Point Exception from the garbage collector. We disable the SIGFPE set by
   // Trick, create the RTI-Ambassador, and then enable the SIGFPE again. This
   // will allow the JVM to start up its threads without the SIGFPE set. See
   // Pitch RTI bug case #9704.
   // TODO: Is this still necessary?
   bool trick_sigfpe_is_set = ( exec_get_trap_sigfpe() > 0 );
   if ( trick_sigfpe_is_set ) {
      exec_set_trap_sigfpe( false );
   }

   // For HLA-Evolved, the user can set a vendor specific local settings for
   // the connect() API.
   if ( should_print( DEBUG_LEVEL_2_TRACE, DEBUG_SOURCE_FEDERATE ) ) {
      if ( ( local_settings == NULL ) || ( *local_settings == '\0' ) ) {
         ostringstream msg;
         msg << "Federate::create_RTI_ambassador_and_connect():" << __LINE__
             << " WARNING: Local settings designator 'THLA.federate.local_settings'"
             << " for the RTI-Ambassador connection was not specified in the"
             << " input file, using HLA-Evolved vendor defaults." << THLA_ENDL;
         send_hs( stdout, (char *)msg.str().c_str() );
      } else {
         ostringstream msg;
         msg << "Federate::create_RTI_ambassador_and_connect():" << __LINE__
             << " Local settings designator for RTI-Ambassador connection:\n'"
             << local_settings << "'" << THLA_ENDL;
         send_hs( stdout, (char *)msg.str().c_str() );
      }
   }

   try {
      // Create the RTI ambassador factory.
      RTIambassadorFactory *rtiAmbassadorFactory = new RTIambassadorFactory();

      // Create the RTI ambassador.
      this->RTI_ambassador = rtiAmbassadorFactory->createRTIambassador();

      if ( ( local_settings == NULL ) || ( *local_settings == '\0' ) ) {
         // Use default vendor local settings.
         RTI_ambassador->connect( *federate_ambassador, HLA_IMMEDIATE );
      } else {
         wstring local_settings_ws;
         StringUtilities::to_wstring( local_settings_ws, local_settings );

         RTI_ambassador->connect( *federate_ambassador, HLA_IMMEDIATE, local_settings_ws );
      }

      // Make sure we delete the factory now that we are done with it.
      delete rtiAmbassadorFactory;

   } catch ( ConnectionFailed &e ) {
      // Macro to restore the saved FPU Control Word register value.
      TRICKHLA_RESTORE_FPU_CONTROL_WORD;
      TRICKHLA_VALIDATE_FPU_CONTROL_WORD;

      string rti_err_msg;
      StringUtilities::to_string( rti_err_msg, e.what() );

      ostringstream errmsg;
      errmsg << "Federate::create_RTI_ambassador_and_connect():" << __LINE__
             << " For Federate: '" << name
             << "' of Federation: '" << federation_name
             << "' with local_settings: '" << ( ( local_settings != NULL ) ? local_settings : "" )
             << "' got EXCEPTION: ConnectionFailed: '" << rti_err_msg << "'."
             << THLA_ENDL;
      send_hs( stderr, (char *)errmsg.str().c_str() );
      exec_terminate( __FILE__, (char *)errmsg.str().c_str() );
   } catch ( InvalidLocalSettingsDesignator &e ) {
      // Macro to restore the saved FPU Control Word register value.
      TRICKHLA_RESTORE_FPU_CONTROL_WORD;
      TRICKHLA_VALIDATE_FPU_CONTROL_WORD;

      ostringstream errmsg;
      errmsg << "Federate::create_RTI_ambassador_and_connect():" << __LINE__
             << " For Federate: '" << name
             << "' of Federation: '" << federation_name
             << "' with local_settings: '" << ( ( local_settings != NULL ) ? local_settings : "" )
             << "' got EXCEPTION: InvalidLocalSettingsDesignator" << THLA_ENDL;
      send_hs( stderr, (char *)errmsg.str().c_str() );
      exec_terminate( __FILE__, (char *)errmsg.str().c_str() );
   } catch ( UnsupportedCallbackModel &e ) {
      // Macro to restore the saved FPU Control Word register value.
      TRICKHLA_RESTORE_FPU_CONTROL_WORD;
      TRICKHLA_VALIDATE_FPU_CONTROL_WORD;

      ostringstream errmsg;
      errmsg << "Federate::create_RTI_ambassador_and_connect():" << __LINE__
             << " For Federate: '" << name
             << "' of Federation: '" << federation_name
             << "' with local_settings: '" << ( ( local_settings != NULL ) ? local_settings : "" )
             << "' got EXCEPTION: UnsupportedCallbackModel" << THLA_ENDL;
      send_hs( stderr, (char *)errmsg.str().c_str() );
      exec_terminate( __FILE__, (char *)errmsg.str().c_str() );
   } catch ( AlreadyConnected &e ) {
      // Macro to restore the saved FPU Control Word register value.
      TRICKHLA_RESTORE_FPU_CONTROL_WORD;
      TRICKHLA_VALIDATE_FPU_CONTROL_WORD;

      ostringstream errmsg;
      errmsg << "Federate::create_RTI_ambassador_and_connect()"
             << " For Federate: '" << name
             << "' of Federation: '" << federation_name
             << "' with local_settings: '" << ( ( local_settings != NULL ) ? local_settings : "" )
             << "' got EXCEPTION: AlreadyConnected" << THLA_ENDL;
      send_hs( stderr, (char *)errmsg.str().c_str() );
      exec_terminate( __FILE__, (char *)errmsg.str().c_str() );
   } catch ( CallNotAllowedFromWithinCallback &e ) {
      // Macro to restore the saved FPU Control Word register value.
      TRICKHLA_RESTORE_FPU_CONTROL_WORD;
      TRICKHLA_VALIDATE_FPU_CONTROL_WORD;

      ostringstream errmsg;
      errmsg << "Federate::create_RTI_ambassador_and_connect():" << __LINE__
             << " For Federate: '" << name
             << "' of Federation: '" << federation_name
             << "' with local_settings: '" << ( ( local_settings != NULL ) ? local_settings : "" )
             << "' got EXCEPTION: CallNotAllowedFromWithinCallback" << THLA_ENDL;
      send_hs( stderr, (char *)errmsg.str().c_str() );
      exec_terminate( __FILE__, (char *)errmsg.str().c_str() );
   } catch ( RTIinternalError &e ) {
      // Macro to restore the saved FPU Control Word register value.
      TRICKHLA_RESTORE_FPU_CONTROL_WORD;
      TRICKHLA_VALIDATE_FPU_CONTROL_WORD;

      string rti_err_msg;
      StringUtilities::to_string( rti_err_msg, e.what() );

      ostringstream errmsg;
      errmsg << "Federate::create_RTI_ambassador_and_connect():" << __LINE__
             << " For Federate: '" << name
             << "' of Federation: '" << federation_name
             << "' with local_settings: '" << ( ( local_settings != NULL ) ? local_settings : "" )
             << "' got RTIinternalError: '" << rti_err_msg
             << "'. One possible"
             << " cause could be that the Central RTI Component is not running,"
             << " or is not running on the computer you think it is on. Please"
             << " check your CRC host and port settings and make sure the RTI"
             << " is running." << THLA_ENDL;
      send_hs( stderr, (char *)errmsg.str().c_str() );
      exec_terminate( __FILE__, (char *)errmsg.str().c_str() );
   }

   // Macro to restore the saved FPU Control Word register value.
   TRICKHLA_RESTORE_FPU_CONTROL_WORD;
   TRICKHLA_VALIDATE_FPU_CONTROL_WORD;

   if ( trick_sigfpe_is_set ) {
      exec_set_trap_sigfpe( true );
   }
}

void Federate::add_federate_instance_id(
   ObjectInstanceHandle instance_hndl )
{
   joined_federate_name_map[instance_hndl] = L"";
}

void Federate::remove_federate_instance_id(
   ObjectInstanceHandle instance_hndl )
{
   TrickHLAObjInstanceNameMap::iterator iter;
   iter = joined_federate_name_map.find( instance_hndl );

   if ( iter != joined_federate_name_map.end() ) {
      joined_federate_name_map.erase( iter );
   }
}

bool Federate::is_federate_instance_id(
   ObjectInstanceHandle id )
{
   return ( joined_federate_name_map.find( id ) != joined_federate_name_map.end() );
}

void Federate::set_MOM_HLAfederate_instance_attributes(
   ObjectInstanceHandle           id,
   AttributeHandleValueMap const &values )
{
   // Add the federate ID if we don't know about it already.
   if ( !is_federate_instance_id( id ) ) {
      add_federate_instance_id( id );
   }

   wstring                                 federate_name_ws( L"" );
   AttributeHandleValueMap::const_iterator attr_iter;

   // Find the Federate name for the given MOM federate Name attribute handle.
   attr_iter = values.find( MOM_HLAfederateName_handle );

   // Determine if we have a federate name attribute.
   if ( attr_iter != values.end() ) {

      // Extract the size of the data and the data bytes.
      size_t num_bytes = attr_iter->second.size();
      char * data      = (char *)attr_iter->second.data();

      // The Federate name is encoded in the HLAunicodeString format. The first
      // four bytes represent the number of two-byte characters that are in the
      // string. For example, a federate name of "CEV" would have the following
      // ASCII decimal values in the character array:
      //  0 0 0 3 0 67 0 69 0 86
      //  ---+---    |    |    |
      //     |       |    |    |
      // size = 3    C    E    V
      //
      for ( size_t i = 5; i < num_bytes; i += 2 ) {
         federate_name_ws.append( data + i, data + i + 1 );
      }

      joined_federate_name_map[id] = federate_name_ws;

      // Make sure that the federate name does not exist before adding...
      bool found = false;
      for ( unsigned int i = 0; !found && ( i < joined_federate_names.size() ); ++i ) {
         if ( joined_federate_names[i] == federate_name_ws ) {
            found = true;
            break; // exit 'for loop'
         }
      }

      if ( !found ) {
         joined_federate_names.push_back( federate_name_ws );
      }

      if ( should_print( DEBUG_LEVEL_2_TRACE, DEBUG_SOURCE_FEDERATE ) ) {
         string id_str;
         StringUtilities::to_string( id_str, id );
         send_hs( stdout, "Federate::set_MOM_HLAfederate_instance_attributes():%d Federate OID:%s name:'%ls' size:%d %c",
                  __LINE__, id_str.c_str(), federate_name_ws.c_str(),
                  (int)federate_name_ws.size(), THLA_NEWLINE );
      }
   }

   // Find the FederateHandle attribute for the given MOM federate handle.
   attr_iter = values.find( MOM_HLAfederate_handle );

   // Determine if we have a federate handle attribute.
   if ( attr_iter != values.end() ) {

      // Do a sanity check on the overall encoded data size.
      if ( attr_iter->second.size() != 8 ) {
         ostringstream errmsg;
         errmsg << "Federate::set_MOM_HLAfederate_instance_attributes():"
                << __LINE__ << " Unexpected number of bytes in the"
                << " Encoded FederateHandle because the byte count is "
                << attr_iter->second.size()
                << " but we expected 8!" << THLA_ENDL;
         send_hs( stderr, (char *)errmsg.str().c_str() );
         exec_terminate( __FILE__, (char *)errmsg.str().c_str() );
         exit( 1 );
      }

      // The HLAfederateHandle has the HLAhandle datatype which is has the
      // HLAvariableArray encoding with an HLAbyte element type.
      //  0 0 0 4 0 0 0 2
      //  ---+--- | | | |
      //     |    ---+---
      // #elem=4  fedID = 2
      //
      // First 4 bytes (first 32-bit integer) is the number of elements.
      // Decode size from Big Endian encoded integer.
      unsigned char *dataPtr = (unsigned char *)attr_iter->second.data();
      size_t         size    = Utilities::is_transmission_byteswap( ENCODING_BIG_ENDIAN ) ? (size_t)Utilities::byteswap_int( *(int *)dataPtr ) : ( size_t ) * (int *)dataPtr;
      if ( size != 4 ) {
         ostringstream errmsg;
         errmsg << "Federate::set_MOM_HLAfederate_instance_attributes():"
                << __LINE__ << " FederateHandle size is "
                << size << " but expected it to be 4!" << THLA_ENDL;
         send_hs( stderr, (char *)errmsg.str().c_str() );
         exec_terminate( __FILE__, (char *)errmsg.str().c_str() );
         exit( 1 );
      }

      // Point to the start of the federate handle ID in the encoded data.
      dataPtr += 4;

      VariableLengthData handle_data;
      handle_data.setData( dataPtr, size );

      FederateHandle tHandle;

      // Macro to save the FPU Control Word register value.
      TRICKHLA_SAVE_FPU_CONTROL_WORD;

      try {
         tHandle = RTI_ambassador->decodeFederateHandle( handle_data );
      } catch ( CouldNotDecode &e ) {
         // Macro to restore the saved FPU Control Word register value.
         TRICKHLA_RESTORE_FPU_CONTROL_WORD;
         TRICKHLA_VALIDATE_FPU_CONTROL_WORD;

         ostringstream errmsg;
         errmsg << "Federate::set_MOM_HLAfederate_instance_attributes():" << __LINE__
                << "when decoding 'FederateHandle': EXCEPTION: CouldNotDecode"
                << THLA_ENDL;
         send_hs( stderr, (char *)errmsg.str().c_str() );
         exec_terminate( __FILE__, (char *)errmsg.str().c_str() );
         exit( 1 );
      } catch ( FederateNotExecutionMember &e ) {
         // Macro to restore the saved FPU Control Word register value.
         TRICKHLA_RESTORE_FPU_CONTROL_WORD;
         TRICKHLA_VALIDATE_FPU_CONTROL_WORD;

         ostringstream errmsg;
         errmsg << "Federate::set_MOM_HLAfederate_instance_attributes():" << __LINE__
                << "when decoding 'FederateHandle': EXCEPTION: FederateNotExecutionMember"
                << THLA_ENDL;
         send_hs( stderr, (char *)errmsg.str().c_str() );
         exec_terminate( __FILE__, (char *)errmsg.str().c_str() );
         exit( 1 );
      } catch ( NotConnected &e ) {
         // Macro to restore the saved FPU Control Word register value.
         TRICKHLA_RESTORE_FPU_CONTROL_WORD;
         TRICKHLA_VALIDATE_FPU_CONTROL_WORD;

         ostringstream errmsg;
         errmsg << "Federate::set_MOM_HLAfederate_instance_attributes():" << __LINE__
                << "when decoding 'FederateHandle': EXCEPTION: NotConnected"
                << THLA_ENDL;
         send_hs( stderr, (char *)errmsg.str().c_str() );
         exec_terminate( __FILE__, (char *)errmsg.str().c_str() );
         exit( 1 );
      } catch ( RTIinternalError &e ) {
         // Macro to restore the saved FPU Control Word register value.
         TRICKHLA_RESTORE_FPU_CONTROL_WORD;
         TRICKHLA_VALIDATE_FPU_CONTROL_WORD;

         string rti_err_msg;
         StringUtilities::to_string( rti_err_msg, e.what() );

         ostringstream errmsg;
         errmsg << "Federate::set_MOM_HLAfederate_instance_attributes():" << __LINE__
                << "when decoding 'FederateHandle': EXCEPTION: "
                << "RTIinternalError: %s" << rti_err_msg << THLA_ENDL;
         send_hs( stderr, (char *)errmsg.str().c_str() );
         exec_terminate( __FILE__, (char *)errmsg.str().c_str() );
         exit( 1 );
      }

      // Macro to restore the saved FPU Control Word register value.
      TRICKHLA_RESTORE_FPU_CONTROL_WORD;
      TRICKHLA_VALIDATE_FPU_CONTROL_WORD;

      // Add this FederateHandle to the set of joined federates.
      joined_federate_handles.insert( tHandle );

      if ( should_print( DEBUG_LEVEL_2_TRACE, DEBUG_SOURCE_FEDERATE ) ) {
         string id_str, fed_id;
         StringUtilities::to_string( id_str, id );
         StringUtilities::to_string( fed_id, tHandle );
         send_hs( stdout, "Federate::set_MOM_HLAfederate_instance_attributes():%d Federate-OID:%s num_bytes:%d Federate-ID:%s %c",
                  __LINE__, id_str.c_str(), size, fed_id.c_str(), THLA_NEWLINE );
      }

      // If this federate is running, add the new entry into running_feds...
      if ( is_federate_executing() ) {
         bool found = false;
         for ( int loop = 0; loop < running_feds_count; loop++ ) {
            char *tName = StringUtilities::ip_strdup_wstring( federate_name_ws );
            if ( !strcmp( running_feds[loop].name, tName ) ) {
               found = true;
               break;
            }
         }
         // update the running_feds if the federate name was not found...
         if ( !found ) {
            if ( joined_federate_name_map.size() == 1 ) {
               add_a_single_entry_into_running_feds();

               // clear the entry after it is absorbed into running_feds...
               joined_federate_name_map.clear();
            } else {
               // loop thru all joined_federate_name_map entries removing stray
               // NULL string entries
               TrickHLAObjInstanceNameMap::iterator map_iter;
               for ( map_iter = joined_federate_name_map.begin();
                     map_iter != joined_federate_name_map.end(); ++map_iter ) {
                  if ( map_iter->second.length() == 0 ) {
                     joined_federate_name_map.erase( map_iter );

                     // Re-process all entries if any are deleted since the
                     // delete modified the iterator position.
                     map_iter = joined_federate_name_map.begin();
                  }
               }

               // After the purge, if there is only one value, process the
               // single element...
               if ( joined_federate_name_map.size() == 1 ) {
                  add_a_single_entry_into_running_feds();

                  // clear the entry after it is absorbed into running_feds...
                  joined_federate_name_map.clear();
               } else {
                  // process multiple joined_federate_name_map entries
                  clear_running_feds();
                  running_feds_count++;
                  update_running_feds();

                  // clear the entries after they are absorbed into running_feds...
                  joined_federate_name_map.clear();
               }
            }
         }
      }
   } else {
      if ( should_print( DEBUG_LEVEL_2_TRACE, DEBUG_SOURCE_FEDERATE ) ) {
         string id_str;
         StringUtilities::to_string( id_str, id );
         send_hs( stdout, "Federate::set_MOM_HLAfederate_instance_attributes():%d FederateHandle Not found for Federate OID:%s %c",
                  __LINE__, id_str.c_str(), THLA_NEWLINE );
      }
   }
}

void Federate::set_all_federate_MOM_instance_handles_by_name()
{
   // Make sure the discovered federate instances list is cleared.
   joined_federate_name_map.clear();

   RTIambassador *rti_amb = get_RTI_ambassador();
   if ( rti_amb == NULL ) {
      send_hs( stderr, "Federate::set_all_federate_MOM_instance_handles_by_name():%d Unexpected NULL RTIambassador.%c",
               __LINE__, THLA_NEWLINE );
      exec_terminate( __FILE__, "Federate::set_all_federate_MOM_instance_handles_by_name() Unexpected NULL RTIambassador." );
      return;
   }

   wstring fed_mom_instance_name_ws = L"";

   ostringstream summary;
   if ( should_print( DEBUG_LEVEL_2_TRACE, DEBUG_SOURCE_FEDERATE ) ) {
      summary << "Federate::set_all_federate_MOM_instance_handles_by_name():" << __LINE__;
   }

   // Macro to save the FPU Control Word register value.
   TRICKHLA_SAVE_FPU_CONTROL_WORD;

   // Resolve all the federate instance handles given the federate names.
   try {
      for ( int i = 0; i < known_feds_count; i++ ) {
         if ( known_feds[i].MOM_instance_name != NULL ) {

            // Create the wide-string version of the MOM instance name.
            StringUtilities::to_wstring( fed_mom_instance_name_ws,
                                         known_feds[i].MOM_instance_name );

            // Get the instance handle based on the instance name.
            ObjectInstanceHandle fed_mom_obj_instance_hdl =
               rti_amb->getObjectInstanceHandle( fed_mom_instance_name_ws );

            // Add the federate instance handle.
            add_federate_instance_id( fed_mom_obj_instance_hdl );

            if ( should_print( DEBUG_LEVEL_2_TRACE, DEBUG_SOURCE_FEDERATE ) ) {
               string id_str;
               StringUtilities::to_string( id_str, fed_mom_obj_instance_hdl );
               summary << "\n    Federate:'" << known_feds[i].name
                       << "' MOM-Object-ID:" << id_str.c_str();
            }
         }
      }
   } catch ( ObjectInstanceNotKnown &e ) {
      // Macro to restore the saved FPU Control Word register value.
      TRICKHLA_RESTORE_FPU_CONTROL_WORD;
      TRICKHLA_VALIDATE_FPU_CONTROL_WORD;

      if ( should_print( DEBUG_LEVEL_2_TRACE, DEBUG_SOURCE_FEDERATE ) ) {
         summary << THLA_ENDL;
         send_hs( stdout, (char *)summary.str().c_str() );
      }

      ostringstream errmsg;
      errmsg << "Federate::set_all_federate_MOM_instance_handles_by_name():" << __LINE__
             << " ERROR: Object Instance Not Known for '"
             << fed_mom_instance_name_ws.c_str() << "'" << THLA_ENDL;
      send_hs( stderr, (char *)errmsg.str().c_str() );
      exec_terminate( __FILE__, (char *)errmsg.str().c_str() );
   } catch ( FederateNotExecutionMember &e ) {
      // Macro to restore the saved FPU Control Word register value.
      TRICKHLA_RESTORE_FPU_CONTROL_WORD;
      TRICKHLA_VALIDATE_FPU_CONTROL_WORD;

      if ( should_print( DEBUG_LEVEL_2_TRACE, DEBUG_SOURCE_FEDERATE ) ) {
         summary << THLA_ENDL;
         send_hs( stdout, (char *)summary.str().c_str() );
      }

      ostringstream errmsg;
      errmsg << "Federate::set_all_federate_MOM_instance_handles_by_name():" << __LINE__
             << " ERROR: Federation Not Execution Member" << THLA_ENDL;
      send_hs( stderr, (char *)errmsg.str().c_str() );
      exec_terminate( __FILE__, (char *)errmsg.str().c_str() );
   } catch ( NotConnected &e ) {
      // Macro to restore the saved FPU Control Word register value.
      TRICKHLA_RESTORE_FPU_CONTROL_WORD;
      TRICKHLA_VALIDATE_FPU_CONTROL_WORD;

      if ( should_print( DEBUG_LEVEL_2_TRACE, DEBUG_SOURCE_FEDERATE ) ) {
         summary << THLA_ENDL;
         send_hs( stdout, (char *)summary.str().c_str() );
      }

      ostringstream errmsg;
      errmsg << "Federate::set_all_federate_MOM_instance_handles_by_name():" << __LINE__
             << " ERROR: NotConnected" << THLA_ENDL;
      send_hs( stderr, (char *)errmsg.str().c_str() );
      exec_terminate( __FILE__, (char *)errmsg.str().c_str() );
   } catch ( RTIinternalError &e ) {
      // Macro to restore the saved FPU Control Word register value.
      TRICKHLA_RESTORE_FPU_CONTROL_WORD;
      TRICKHLA_VALIDATE_FPU_CONTROL_WORD;

      if ( should_print( DEBUG_LEVEL_2_TRACE, DEBUG_SOURCE_FEDERATE ) ) {
         summary << THLA_ENDL;
         send_hs( stdout, (char *)summary.str().c_str() );
      }

      string rti_err_msg;
      StringUtilities::to_string( rti_err_msg, e.what() );

      ostringstream errmsg;
      errmsg << "Federate::set_all_federate_MOM_instance_handles_by_name():" << __LINE__
             << " RTIinternalError: '" << rti_err_msg << "'" << THLA_ENDL;
      send_hs( stderr, (char *)errmsg.str().c_str() );
      exec_terminate( __FILE__, (char *)errmsg.str().c_str() );
   } catch ( RTI1516_EXCEPTION &e ) {
      // Macro to restore the saved FPU Control Word register value.
      TRICKHLA_RESTORE_FPU_CONTROL_WORD;
      TRICKHLA_VALIDATE_FPU_CONTROL_WORD;

      if ( should_print( DEBUG_LEVEL_2_TRACE, DEBUG_SOURCE_FEDERATE ) ) {
         summary << THLA_ENDL;
         send_hs( stdout, (char *)summary.str().c_str() );
      }

      string rti_err_msg;
      StringUtilities::to_string( rti_err_msg, e.what() );

      ostringstream errmsg;
      errmsg << "Federate::set_all_federate_MOM_instance_handles_by_name():" << __LINE__
             << " RTI1516_EXCEPTION for '" << rti_err_msg << "'" << THLA_ENDL;
      send_hs( stderr, (char *)errmsg.str().c_str() );
      exec_terminate( __FILE__, (char *)errmsg.str().c_str() );
   }
   // Macro to restore the saved FPU Control Word register value.
   TRICKHLA_RESTORE_FPU_CONTROL_WORD;
   TRICKHLA_VALIDATE_FPU_CONTROL_WORD;

   if ( should_print( DEBUG_LEVEL_2_TRACE, DEBUG_SOURCE_FEDERATE ) ) {
      summary << THLA_ENDL;
      send_hs( stdout, (char *)summary.str().c_str() );
   }
}

/*!
 *  @job_class{initialization}
 */
void Federate::determine_federate_MOM_object_instance_names()
{
   RTIambassador *rti_amb = get_RTI_ambassador();
   if ( rti_amb == NULL ) {
      send_hs( stderr, "Federate::determine_federate_MOM_object_instance_names():%d Unexpected NULL RTIambassador.%c",
               __LINE__, THLA_NEWLINE );
      exec_terminate( __FILE__, "Federate::determine_federate_MOM_object_instance_names() Unexpected NULL RTIambassador." );
      return;
   }

   wstring                                    fed_name_ws = L"";
   ObjectInstanceHandle                       fed_mom_instance_hdl;
   TrickHLAObjInstanceNameMap::const_iterator fed_iter;

   // Macro to save the FPU Control Word register value.
   TRICKHLA_SAVE_FPU_CONTROL_WORD;

   try {
      for ( fed_iter = joined_federate_name_map.begin();
            fed_iter != joined_federate_name_map.end(); ++fed_iter ) {

         for ( int i = 0; i < known_feds_count; i++ ) {
            StringUtilities::to_wstring( fed_name_ws, known_feds[i].name );

            if ( fed_iter->second.compare( fed_name_ws ) == 0 ) {
               fed_mom_instance_hdl = fed_iter->first;

               // Get the instance name based on the MOM object instance handle
               // and make sure it is in the Trick memory space.
               known_feds[i].MOM_instance_name =
                  StringUtilities::ip_strdup_wstring(
                     rti_amb->getObjectInstanceName( fed_mom_instance_hdl ) );
            }
         }
      }
   } catch ( ObjectInstanceNotKnown &e ) {
      // Macro to restore the saved FPU Control Word register value.
      TRICKHLA_RESTORE_FPU_CONTROL_WORD;
      TRICKHLA_VALIDATE_FPU_CONTROL_WORD;
      send_hs( stderr, "Object::register_object_with_RTI():%d rti_amb->getObjectInstanceName() ERROR: ObjectInstanceNotKnown%c",
               __LINE__, THLA_NEWLINE );
   } catch ( FederateNotExecutionMember &e ) {
      // Macro to restore the saved FPU Control Word register value.
      TRICKHLA_RESTORE_FPU_CONTROL_WORD;
      TRICKHLA_VALIDATE_FPU_CONTROL_WORD;
      send_hs( stderr, "Object::register_object_with_RTI():%d rti_amb->getObjectInstanceName() ERROR: FederateNotExecutionMember%c",
               __LINE__, THLA_NEWLINE );
   } catch ( NotConnected &e ) {
      // Macro to restore the saved FPU Control Word register value.
      TRICKHLA_RESTORE_FPU_CONTROL_WORD;
      TRICKHLA_VALIDATE_FPU_CONTROL_WORD;
      send_hs( stderr, "Object::register_object_with_RTI():%d rti_amb->getObjectInstanceName() ERROR: NotConnected%c",
               __LINE__, THLA_NEWLINE );
   } catch ( RTIinternalError &e ) {
      // Macro to restore the saved FPU Control Word register value.
      TRICKHLA_RESTORE_FPU_CONTROL_WORD;
      TRICKHLA_VALIDATE_FPU_CONTROL_WORD;
      string rti_err_msg;
      StringUtilities::to_string( rti_err_msg, e.what() );
      send_hs( stderr, "Object::register_object_with_RTI():%d rti_amb->getObjectInstanceName() ERROR: RTIinternalError: '%s'%c",
               __LINE__, rti_err_msg.c_str(), THLA_NEWLINE );
   } catch ( RTI1516_EXCEPTION &e ) {
      // Macro to restore the saved FPU Control Word register value.
      TRICKHLA_RESTORE_FPU_CONTROL_WORD;
      TRICKHLA_VALIDATE_FPU_CONTROL_WORD;
      string id_str;
      StringUtilities::to_string( id_str, fed_mom_instance_hdl );
      string rti_err_msg;
      StringUtilities::to_string( rti_err_msg, e.what() );
      ostringstream errmsg;
      errmsg << "Object::register_object_with_RTI():" << __LINE__
             << " Exception getting MOM instance name for '"
             << fed_name_ws.c_str() << "' ID:" << id_str
             << " '" << rti_err_msg << "'." << THLA_ENDL;
      send_hs( stderr, (char *)errmsg.str().c_str() );
      exec_terminate( __FILE__, (char *)errmsg.str().c_str() );
   }
   // Macro to restore the saved FPU Control Word register value.
   TRICKHLA_RESTORE_FPU_CONTROL_WORD;
   TRICKHLA_VALIDATE_FPU_CONTROL_WORD;
}

bool Federate::is_required_federate(
   wstring const &federate_name )
{
   for ( int i = 0; i < known_feds_count; i++ ) {
      if ( known_feds[i].required ) {
         wstring required_fed_name;
         StringUtilities::to_wstring( required_fed_name, known_feds[i].name );
         if ( federate_name == required_fed_name ) {
            return true;
         }
      }
   }
   return false;
}

bool Federate::is_joined_federate(
   const char *federate_name )
{
   wstring fed_name_ws;
   StringUtilities::to_wstring( fed_name_ws, federate_name );
   return is_joined_federate( fed_name_ws );
}

bool Federate::is_joined_federate(
   wstring const &federate_name )
{
   for ( unsigned int i = 0; i < joined_federate_names.size(); i++ ) {
      if ( federate_name == joined_federate_names[i] ) {
         return true;
      }
   }
   return false;
}

/*!
 *  @job_class{initialization}
 */
string Federate::wait_for_required_federates_to_join()
{
   string tRetString;

   // If the known Federates list is disabled then just return.
   if ( !enable_known_feds ) {
      if ( should_print( DEBUG_LEVEL_2_TRACE, DEBUG_SOURCE_FEDERATE ) ) {
         send_hs( stdout, "Federate::wait_for_required_federates_to_join():%d Check for required Federates DISABLED.%c",
                  __LINE__, THLA_NEWLINE );
      }
      return tRetString;
   }

   // Determine how many required federates we have.
   unsigned int requiredFedsCount = 0;
   for ( int i = 0; i < known_feds_count; i++ ) {
      if ( known_feds[i].required ) {
         requiredFedsCount++;
      }
   }

   // If we don't have any required Federates then return.
   if ( requiredFedsCount == 0 ) {
      if ( should_print( DEBUG_LEVEL_2_TRACE, DEBUG_SOURCE_FEDERATE ) ) {
         send_hs( stdout, "Federate::wait_for_required_federates_to_join():%d NO REQUIRED FEDERATES!!!%c",
                  __LINE__, THLA_NEWLINE );
      }
      return tRetString;
   }

   // Make sure we clear the joined federate handle set.
   joined_federate_handles.clear();

   // Create a summary of the required federates.
   if ( should_print( DEBUG_LEVEL_2_TRACE, DEBUG_SOURCE_FEDERATE ) ) {
      ostringstream required_fed_summary;
      required_fed_summary << "Federate::wait_for_required_federates_to_join():"
                           << __LINE__ << "\nWAITING FOR " << requiredFedsCount
                           << " REQUIRED FEDERATES:";

      // Display the initial summary of the required federates we are waiting for.
      int cnt = 0;
      for ( int i = 0; i < known_feds_count; i++ ) {
         // Create a summary of the required federates by name.
         if ( known_feds[i].required ) {
            cnt++;
            required_fed_summary << "\n    " << cnt
                                 << ": Waiting for required federate '"
                                 << known_feds[i].name << "'";
         }
      }

      required_fed_summary << THLA_ENDL;

      // Display a summary of the required federate by name.
      send_hs( stdout, (char *)required_fed_summary.str().c_str() );

      // Display a message that we are requesting the federate names.
      send_hs( stdout, "Federate::wait_for_required_federates_to_join():%d Requesting list of joined federates from CRC.%c",
               __LINE__, THLA_NEWLINE );
   }

   // Subscribe to Federate names using MOM interface and request an update.
   ask_MOM_for_federate_names();

   size_t i, reqFedCnt;
   size_t joinedFedCount = 0;

   // Wait for all the required federates to join.
   all_federates_joined = false;

   bool          found_an_unrequired_federate = false;
   set< string > unrequired_federates_list; // list of unique unrequired federate names
   unsigned int  sleep_micros = 1000;
   unsigned int  wait_count   = 0;
   unsigned int  wait_check   = 10000000 / sleep_micros; // Number of wait cycles for 10 seconds

   while ( !all_federates_joined ) {

      // Check for shutdown.
      this->check_for_shutdown_with_termination();

      // Sleep a little while to wait for more federates to join.
      usleep( sleep_micros );

      // Determine what federates have joined only if the joined federate
      // count has changed.
      if ( joinedFedCount != joined_federate_names.size() ) {
         joinedFedCount = joined_federate_names.size();

         // Count the number of joined Required federates.
         reqFedCnt = 0;
         for ( i = 0; i < joined_federate_names.size(); i++ ) {
            if ( is_required_federate( joined_federate_names[i] ) ) {
               reqFedCnt++;
            } else {
               found_an_unrequired_federate = true;
               string fedname;
               StringUtilities::to_string( fedname,
                                           joined_federate_names[i] );
               if ( restore_is_imminent ) {
                  if ( should_print( DEBUG_LEVEL_2_TRACE, DEBUG_SOURCE_FEDERATE ) ) {
                     send_hs( stdout, "Federate::wait_for_required_federates_to_join():%d Found an UNREQUIRED federate %s!%c",
                              __LINE__, fedname.c_str(), THLA_NEWLINE );
                  }
                  unrequired_federates_list.insert( fedname );
               }
            }
         }

         // Determine if all the Required federates have joined.
         if ( reqFedCnt >= requiredFedsCount ) {
            all_federates_joined = true;
         }

         // Print out a list of the Joined Federates.
         if ( should_print( DEBUG_LEVEL_2_TRACE, DEBUG_SOURCE_FEDERATE ) ) {
            // Build the federate summary as an output string stream.
            ostringstream summary;
            unsigned int  cnt = 0;

            summary << "Federate::wait_for_required_federates_to_join():"
                    << __LINE__ << "\nWAITING FOR " << requiredFedsCount
                    << " REQUIRED FEDERATES:";

            // Summarize the required federates first.
            for ( i = 0; i < (unsigned int)known_feds_count; i++ ) {
               cnt++;
               if ( known_feds[i].required ) {
                  if ( is_joined_federate( known_feds[i].name ) ) {
                     summary << "\n    " << cnt
                             << ": Found joined required federate '"
                             << known_feds[i].name << "'";
                  } else {
                     summary << "\n    " << cnt
                             << ": Waiting for required federate '"
                             << known_feds[i].name << "'";
                  }
               }
            }

            // Summarize all the remaining non-required joined federates.
            for ( i = 0; i < joined_federate_names.size(); i++ ) {
               if ( !is_required_federate( joined_federate_names[i] ) ) {
                  cnt++;

                  // We need a string version of the wide-string federate name.
                  string fedname;
                  StringUtilities::to_string(
                     fedname, joined_federate_names[i] );

                  summary << "\n    " << cnt << ": Found joined federate '"
                          << fedname.c_str() << "'";
               }
            }
            summary << THLA_ENDL;

            // Display the federate summary.
            send_hs( stdout, (char *)summary.str().c_str() );
         }
      }

      if ( ( !all_federates_joined ) && ( ( ++wait_count % wait_check ) == 0 ) ) {
         wait_count = 0;
         if ( !is_execution_member() ) {
            ostringstream errmsg;
            errmsg << "Federate::wait_for_required_federates_to_join():" << __LINE__
                   << " Unexpectedly the Federate is no longer an execution member."
                   << " This means we are either not connected to the"
                   << " RTI or we are no longer joined to the federation"
                   << " execution because someone forced our resignation at"
                   << " the Central RTI Component (CRC) level!"
                   << THLA_ENDL;
            send_hs( stderr, (char *)errmsg.str().c_str() );
            exec_terminate( __FILE__, (char *)errmsg.str().c_str() );
         }
      }
   }

   // once a list of joined federates has been built, and we are to restore the
   // check if there are any unrequired federates. If any are found, terminate
   // the simulation with a verbose message stating which federates were
   // joined as unrequired, as well as the required federates, so the user
   // knows what happened and know how to properly restart the federation. We
   // do this to inform the user that they did something wrong and gracefully
   //  terminate the execution instead of the federation failing to restore
   // and the user is left to scratch their heads why the federation failed
   // to restore!
   if ( restore_is_imminent && found_an_unrequired_federate ) {
      ostringstream errmsg;
      errmsg << "FATAL ERROR: You indicated a restore of a checkpoint set but "
             << "at least one federate which was NOT executing at the time of "
             << "the checkpoint is currently joined in the federation. This "
             << "violates IEEE Std 1516.2000, section 4.18 (Request Federation "
             << "Restore), precondition d), \"The correct number of joined "
             << "federates of the correct types that were joined to the "
             << "federation execution when the save was accomplished are "
             << "currently joined to the federation execution.\"\n\tThe "
             << "extraneous ";
      if ( unrequired_federates_list.size() == 1 ) {
         errmsg << "federate is: ";
      } else {
         errmsg << "federates are: ";
      }
      set< string >::const_iterator cii;
      string                        tNames;
      for ( cii = unrequired_federates_list.begin();
            cii != unrequired_federates_list.end(); ++cii ) {
         tNames += *cii + ", ";
      }
      tNames = tNames.substr( 0, tNames.length() - 2 ); // remove trailing comma and space
      errmsg << tNames << "\n\tThe required federates are: ";
      tNames = "";
      for ( i = 0; i < (unsigned int)known_feds_count; i++ ) {
         if ( known_feds[i].required ) {
            tNames += known_feds[i].name;
            tNames += ", ";
         }
      }
      tNames = tNames.substr( 0, tNames.length() - 2 ); // remove trailing comma and space
      errmsg << tNames << "\nTERMINATING EXECUTION!";

      tRetString = errmsg.str();
      return tRetString;
   }

   // Unsubscribe from all attributes for the MOM HLAfederate class.
   unsubscribe_all_HLAfederate_class_attributes_from_MOM();

   // Get the federate object instance names so that we can recover the
   // instance handles for the MOM object associated with each federate.
   determine_federate_MOM_object_instance_names();

   if ( should_print( DEBUG_LEVEL_2_TRACE, DEBUG_SOURCE_FEDERATE ) ) {
      send_hs( stdout, "Federate::wait_for_required_federates_to_join():%d FOUND ALL REQUIRED FEDERATES!!!%c",
               __LINE__, THLA_NEWLINE );
   }

   return tRetString;
}

/*!
 *  @job_class{initialization}
 */
void Federate::initialize_MOM_handles()
{
   // Macro to save the FPU Control Word register value.
   TRICKHLA_SAVE_FPU_CONTROL_WORD;

   if ( should_print( DEBUG_LEVEL_2_TRACE, DEBUG_SOURCE_FEDERATE ) ) {
      send_hs( stdout, "Federate::initialize_MOM_handles():%d%c",
               __LINE__, THLA_NEWLINE );
   }

   bool error_flag = false;

   // Get the MOM Federation Class handle.
   try {
      this->MOM_HLAfederation_class_handle = RTI_ambassador->getObjectClassHandle( L"HLAmanager.HLAfederation" );
   } catch ( RTI1516_NAMESPACE::NameNotFound &e ) {
      error_flag = true;
      send_hs( stderr, "Federate::initialize_MOM_handles():%d \
NameNotFound ERROR for RTI_amb->getObjectClassHandle('HLAmanager.HLAfederation'%c",
               __LINE__, THLA_NEWLINE );
   } catch ( RTI1516_NAMESPACE::FederateNotExecutionMember &e ) {
      error_flag = true;
      send_hs( stderr, "Federate::initialize_MOM_handles():%d \
FederateNotExecutionMember ERROR for RTI_amb->getObjectClassHandle('HLAmanager.HLAfederation'%c",
               __LINE__, THLA_NEWLINE );
   } catch ( RTI1516_NAMESPACE::NotConnected &e ) {
      error_flag = true;
      send_hs( stderr, "Federate::initialize_MOM_handles():%d \
NotConnected ERROR for RTI_amb->getObjectClassHandle('HLAmanager.HLAfederation'%c",
               __LINE__, THLA_NEWLINE );
   } catch ( RTI1516_NAMESPACE::RTIinternalError &e ) {
      error_flag = true;
      send_hs( stderr, "Federate::initialize_MOM_handles():%d \
RTIinternalError for RTI_amb->getObjectClassHandle('HLAmanager.HLAfederation')%c",
               __LINE__, THLA_NEWLINE );
   }

   // Get the MOM Federates In Federation Attribute handle.
   try {
      this->MOM_HLAfederatesInFederation_handle = RTI_ambassador->getAttributeHandle( MOM_HLAfederation_class_handle,
                                                                                      L"HLAfederatesInFederation" );
   } catch ( RTI1516_NAMESPACE::InvalidObjectClassHandle &e ) {
      error_flag = true;
      send_hs( stderr, "Federate::initialize_MOM_handles():%d \
InvalidObjectClassHandle ERROR for RTI_amb->getAttributrHandle( MOM_federation_class_handle, \
'HLAfederatesInFederation'%c",
               __LINE__, THLA_NEWLINE );
   } catch ( RTI1516_NAMESPACE::NameNotFound &e ) {
      error_flag = true;
      send_hs( stderr, "Federate::initialize_MOM_handles():%d \
NameNotFound ERROR for RTI_amb->getAttributrHandle( MOM_federation_class_handle, \
'HLAfederatesInFederation'%c",
               __LINE__, THLA_NEWLINE );
   } catch ( RTI1516_NAMESPACE::FederateNotExecutionMember &e ) {
      error_flag = true;
      send_hs( stderr, "Federate::initialize_MOM_handles():%d \
FederateNotExecutionMember ERROR for RTI_amb->getAttributrHandle(MOM_federation_class_handle, \
'HLAfederatesInFederation'%c",
               __LINE__, THLA_NEWLINE );
   } catch ( RTI1516_NAMESPACE::NotConnected &e ) {
      error_flag = true;
      send_hs( stderr, "Federate::initialize_MOM_handles():%d \
NotConnected ERROR for RTI_amb->getAttributrHandle(MOM_federation_class_handle, \
'HLAfederatesInFederation'%c",
               __LINE__, THLA_NEWLINE );
   } catch ( RTI1516_NAMESPACE::RTIinternalError &e ) {
      error_flag = true;
      send_hs( stderr, "Federate::initialize_MOM_handles():%d \
RTIinternalError for RTI_amb->getAttributrHandle( MOM_federation_class_handle, \
'HLAfederatesInFederation'%c",
               __LINE__, THLA_NEWLINE );
   }

   // Get the MOM Auto Provide Attribute handle.
   try {
      this->MOM_HLAautoProvide_handle = RTI_ambassador->getAttributeHandle( MOM_HLAfederation_class_handle,
                                                                            L"HLAautoProvide" );
   } catch ( RTI1516_NAMESPACE::InvalidObjectClassHandle &e ) {
      error_flag = true;
      send_hs( stderr, "Federate::initialize_MOM_handles():%d \
InvalidObjectClassHandle ERROR for RTI_amb->getAttributrHandle( MOM_federation_class_handle, \
'HLAautoProvide'%c",
               __LINE__, THLA_NEWLINE );
   } catch ( RTI1516_NAMESPACE::NameNotFound &e ) {
      error_flag = true;
      send_hs( stderr, "Federate::initialize_MOM_handles():%d \
NameNotFound ERROR for RTI_amb->getAttributrHandle( MOM_federation_class_handle, \
'HLAautoProvide'%c",
               __LINE__, THLA_NEWLINE );
   } catch ( RTI1516_NAMESPACE::FederateNotExecutionMember &e ) {
      error_flag = true;
      send_hs( stderr, "Federate::initialize_MOM_handles():%d \
FederateNotExecutionMember ERROR for RTI_amb->getAttributrHandle(MOM_federation_class_handle, \
'HLAautoProvide'%c",
               __LINE__, THLA_NEWLINE );
   } catch ( RTI1516_NAMESPACE::NotConnected &e ) {
      error_flag = true;
      send_hs( stderr, "Federate::initialize_MOM_handles():%d \
NotConnected ERROR for RTI_amb->getAttributrHandle(MOM_federation_class_handle, \
'HLAautoProvide'%c",
               __LINE__, THLA_NEWLINE );
   } catch ( RTI1516_NAMESPACE::RTIinternalError &e ) {
      error_flag = true;
      send_hs( stderr, "Federate::initialize_MOM_handles():%d \
RTIinternalError for RTI_amb->getAttributrHandle( MOM_federation_class_handle, \
'HLAautoProvide'%c",
               __LINE__, THLA_NEWLINE );
   }

   // Get the MOM Federate Class handle.
   try {
      this->MOM_HLAfederate_class_handle = RTI_ambassador->getObjectClassHandle( L"HLAobjectRoot.HLAmanager.HLAfederate" );
   } catch ( RTI1516_NAMESPACE::NameNotFound &e ) {
      error_flag = true;
      send_hs( stderr, "Federate::initialize_MOM_handles():%d \
NameNotFound ERROR for RTI_amb->getObjectClassHandle('HLAobjectRoot.HLAmanager.HLAfederate')%c",
               __LINE__, THLA_NEWLINE );
   } catch ( RTI1516_NAMESPACE::FederateNotExecutionMember &e ) {
      error_flag = true;
      send_hs( stderr, "Federate::initialize_MOM_handles():%d \
FederateNotExecutionMember ERROR for RTI_amb->getObjectClassHandle('HLAobjectRoot.HLAmanager.HLAfederate')%c",
               __LINE__, THLA_NEWLINE );
   } catch ( RTI1516_NAMESPACE::NotConnected &e ) {
      error_flag = true;
      send_hs( stderr, "Federate::initialize_MOM_handles():%d \
NotConnected ERROR for RTI_amb->getObjectClassHandle('HLAobjectRoot.HLAmanager.HLAfederate')%c",
               __LINE__, THLA_NEWLINE );
   } catch ( RTI1516_NAMESPACE::RTIinternalError &e ) {
      error_flag = true;
      send_hs( stderr, "Federate::initialize_MOM_handles():%d \
RTIinternalError for RTI_amb->getObjectClassHandle('HLAobjectRoot.HLAmanager.HLAfederate')%c",
               __LINE__, THLA_NEWLINE );
   }

   // Get the MOM Federate Name Attribute handle.
   try {
      this->MOM_HLAfederateName_handle = RTI_ambassador->getAttributeHandle( MOM_HLAfederate_class_handle, L"HLAfederateName" );
   } catch ( RTI1516_NAMESPACE::InvalidObjectClassHandle &e ) {
      error_flag = true;
      send_hs( stderr, "Federate::initialize_MOM_handles():%d \
InvalidObjectClassHandle ERROR for RTI_amb->getAttributrHandle(MOM_federate_class_handle, 'HLAfederateName')%c",
               __LINE__, THLA_NEWLINE );
   } catch ( RTI1516_NAMESPACE::NameNotFound &e ) {
      error_flag = true;
      send_hs( stderr, "Federate::initialize_MOM_handles():%d \
NameNotFound ERROR for RTI_amb->getAttributrHandle(MOM_federate_class_handle, 'HLAfederateName')%c",
               __LINE__, THLA_NEWLINE );
   } catch ( RTI1516_NAMESPACE::FederateNotExecutionMember &e ) {
      error_flag = true;
      send_hs( stderr, "Federate::initialize_MOM_handles():%d \
FederateNotExecutionMember ERROR for RTI_amb->getAttributrHandle(MOM_federate_class_handle, 'HLAfederateName')%c",
               __LINE__, THLA_NEWLINE );
   } catch ( RTI1516_NAMESPACE::NotConnected &e ) {
      error_flag = true;
      send_hs( stderr, "Federate::initialize_MOM_handles():%d \
NotConnected ERROR for RTI_amb->getAttributrHandle(MOM_federate_class_handle, \
'HLAfederateName'%c",
               __LINE__, THLA_NEWLINE );
   } catch ( RTI1516_NAMESPACE::RTIinternalError &e ) {
      error_flag = true;
      send_hs( stderr, "Federate::initialize_MOM_handles():%d \
RTIinternalError for RTI_amb->getAttributrHandle(MOM_federate_class_handle, 'HLAfederateName')%c",
               __LINE__, THLA_NEWLINE );
   }

   // Get the MOM Federate Type Attribute handle.
   try {
      this->MOM_HLAfederateType_handle = RTI_ambassador->getAttributeHandle( MOM_HLAfederate_class_handle, L"HLAfederateType" );
   } catch ( RTI1516_NAMESPACE::InvalidObjectClassHandle &e ) {
      error_flag = true;
      send_hs( stderr, "Federate::initialize_MOM_handles():%d \
InvalidObjectClassHandle ERROR for RTI_amb->getAttributrHandle(MOM_federate_class_handle, 'HLAfederateType')%c",
               __LINE__, THLA_NEWLINE );
   } catch ( RTI1516_NAMESPACE::NameNotFound &e ) {
      error_flag = true;
      send_hs( stderr, "Federate::initialize_MOM_handles():%d \
NameNotFound ERROR for RTI_amb->getAttributrHandle(MOM_federate_class_handle, 'HLAfederateType')%c",
               __LINE__, THLA_NEWLINE );
   } catch ( RTI1516_NAMESPACE::FederateNotExecutionMember &e ) {
      error_flag = true;
      send_hs( stderr, "Federate::initialize_MOM_handles():%d \
FederateNotExecutionMember ERROR for RTI_amb->getAttributrHandle(MOM_federate_class_handle, 'HLAfederateType')%c",
               __LINE__, THLA_NEWLINE );
   } catch ( RTI1516_NAMESPACE::NotConnected &e ) {
      error_flag = true;
      send_hs( stderr, "Federate::initialize_MOM_handles():%d \
NotConnected ERROR for RTI_amb->getAttributrHandle(MOM_federate_class_handle, \
'HLAfederateType'%c",
               __LINE__, THLA_NEWLINE );
   } catch ( RTI1516_NAMESPACE::RTIinternalError &e ) {
      error_flag = true;
      send_hs( stderr, "Federate::initialize_MOM_handles():%d \
RTIinternalError for RTI_amb->getAttributrHandle(MOM_federate_class_handle, 'HLAfederateType')%c",
               __LINE__, THLA_NEWLINE );
   }

   // Get the MOM Federate Attribute handle.
   try {
      this->MOM_HLAfederate_handle = RTI_ambassador->getAttributeHandle( MOM_HLAfederate_class_handle, L"HLAfederateHandle" );
   } catch ( RTI1516_NAMESPACE::InvalidObjectClassHandle &e ) {
      error_flag = true;
      send_hs( stderr, "Federate::initialize_MOM_handles():%d \
InvalidObjectClassHandle ERROR for RTI_amb->getAttributrHandle(MOM_federate_class_handle, 'HLAfederateHandle')%c",
               __LINE__, THLA_NEWLINE );
   } catch ( RTI1516_NAMESPACE::NameNotFound &e ) {
      error_flag = true;
      send_hs( stderr, "Federate::initialize_MOM_handles():%d \
NameNotFound ERROR for RTI_amb->getAttributrHandle(MOM_federate_class_handle, 'HLAfederateHandle')%c",
               __LINE__, THLA_NEWLINE );
   } catch ( RTI1516_NAMESPACE::FederateNotExecutionMember &e ) {
      error_flag = true;
      send_hs( stderr, "Federate::initialize_MOM_handles():%d \
FederateNotExecutionMember ERROR for RTI_amb->getAttributrHandle(MOM_federate_class_handle, 'HLAfederateHandle')%c",
               __LINE__, THLA_NEWLINE );
   } catch ( RTI1516_NAMESPACE::NotConnected &e ) {
      error_flag = true;
      send_hs( stderr, "Federate::initialize_MOM_handles():%d \
NotConnected ERROR for RTI_amb->getAttributrHandle(MOM_federate_class_handle, 'HLAfederateHandle')%c",
               __LINE__, THLA_NEWLINE );
   } catch ( RTI1516_NAMESPACE::RTIinternalError &e ) {
      error_flag = true;
      send_hs( stderr, "Federate::initialize_MOM_handles():%d \
RTIinternalError for RTI_amb->getAttributrHandle(MOM_federate_class_handle, 'HLAfederateHandle')%c",
               __LINE__, THLA_NEWLINE );
   }

   // Interaction: HLAmanager.HLAfederation.HLAadjust.HLAsetSwitches
   //   Parameter: HLAautoProvide of type HLAswitches which is a HLAinteger32BE
   try {
      this->MOM_HLAsetSwitches_class_handle = RTI_ambassador->getInteractionClassHandle( L"HLAmanager.HLAfederation.HLAadjust.HLAsetSwitches" );
   } catch ( RTI1516_NAMESPACE::NameNotFound &e ) {
      error_flag = true;
      send_hs( stderr, "Federate::initialize_MOM_handles():%d \
NameNotFound ERROR for RTI_amb->getInteractionClassHandle('HLAmanager.HLAfederation.HLAadjust.HLAsetSwitches')%c",
               __LINE__, THLA_NEWLINE );
   } catch ( RTI1516_NAMESPACE::FederateNotExecutionMember &e ) {
      error_flag = true;
      send_hs( stderr, "Federate::initialize_MOM_handles():%d \
FederateNotExecutionMember ERROR for RTI_amb->getInteractionClassHandle('HLAmanager.HLAfederation.HLAadjust.HLAsetSwitches')%c",
               __LINE__, THLA_NEWLINE );
   } catch ( RTI1516_NAMESPACE::NotConnected &e ) {
      error_flag = true;
      send_hs( stderr, "Federate::initialize_MOM_handles():%d \
NotConnected ERROR for RTI_amb->getInteractionClassHandle('HLAmanager.HLAfederation.HLAadjust.HLAsetSwitches')%c",
               __LINE__, THLA_NEWLINE );
   } catch ( RTI1516_NAMESPACE::RTIinternalError &e ) {
      error_flag = true;
      send_hs( stderr, "Federate::initialize_MOM_handles():%d \
RTIinternalError for RTI_amb->getInteractionClassHandle('HLAmanager.HLAfederation.HLAadjust.HLAsetSwitches')%c",
               __LINE__, THLA_NEWLINE );
   }

   try {
      this->MOM_HLAautoProvide_param_handle = RTI_ambassador->getParameterHandle( MOM_HLAsetSwitches_class_handle, L"HLAautoProvide" );
   } catch ( RTI1516_NAMESPACE::NameNotFound &e ) {
      error_flag = true;
      send_hs( stderr, "Federate::initialize_MOM_handles():%d \
NameNotFound ERROR for RTI_amb->getParameterHandle(MOM_HLAsetSwitches_class_handle, 'HLAautoProvide')%c",
               __LINE__, THLA_NEWLINE );
   } catch ( RTI1516_NAMESPACE::FederateNotExecutionMember &e ) {
      error_flag = true;
      send_hs( stderr, "Federate::initialize_MOM_handles():%d \
FederateNotExecutionMember ERROR for RTI_amb->getParameterHandle(MOM_HLAsetSwitches_class_handle, 'HLAautoProvide')%c",
               __LINE__, THLA_NEWLINE );
   } catch ( RTI1516_NAMESPACE::NotConnected &e ) {
      error_flag = true;
      send_hs( stderr, "Federate::initialize_MOM_handles():%d \
NotConnected ERROR for RTI_amb->getParameterHandle(MOM_HLAsetSwitches_class_handle, 'HLAautoProvide')%c",
               __LINE__, THLA_NEWLINE );
   } catch ( RTI1516_NAMESPACE::RTIinternalError &e ) {
      error_flag = true;
      send_hs( stderr, "Federate::initialize_MOM_handles():%d \
RTIinternalError for RTI_amb->getParameterHandle(MOM_HLAsetSwitches_class_handle, 'HLAautoProvide')%c",
               __LINE__, THLA_NEWLINE );
   }

   // Macro to restore the saved FPU Control Word register value.
   TRICKHLA_RESTORE_FPU_CONTROL_WORD;
   TRICKHLA_VALIDATE_FPU_CONTROL_WORD;

   if ( error_flag ) {
      exec_terminate( __FILE__, "Federate::initialize_MOM_handles() ERROR Detected!" );
   }
}

void Federate::subscribe_attributes(
   ObjectClassHandle         class_handle,
   AttributeHandleSet const &attribute_list )
{
   if ( should_print( DEBUG_LEVEL_4_TRACE, DEBUG_SOURCE_FEDERATE ) ) {
      send_hs( stdout, "Federate::subscribe_attributes():%d%c",
               __LINE__, THLA_NEWLINE );
   }

   // Macro to save the FPU Control Word register value.
   TRICKHLA_SAVE_FPU_CONTROL_WORD;

   bool error_flag = false;

   try {
      RTI_ambassador->subscribeObjectClassAttributes( class_handle, attribute_list, true );
   } catch ( RTI1516_NAMESPACE::ObjectClassNotDefined &e ) {
      error_flag = true;
      send_hs( stderr, "Federate::subscribe_attributes():%d ObjectClassNotDefined: MOM Object Attributed Subscribe FAILED!%c",
               __LINE__, THLA_NEWLINE );
   } catch ( RTI1516_NAMESPACE::AttributeNotDefined &e ) {
      error_flag = true;
      send_hs( stderr, "Federate::subscribe_attributes():%d AttributeNotDefined: MOM Object Attributed Subscribe FAILED!%c",
               __LINE__, THLA_NEWLINE );
   } catch ( RTI1516_NAMESPACE::FederateNotExecutionMember &e ) {
      error_flag = true;
      send_hs( stderr, "Federate::subscribe_attributes():%d FederateNotExecutionMember: MOM Object Attributed Subscribe FAILED!%c",
               __LINE__, THLA_NEWLINE );
   } catch ( RTI1516_NAMESPACE::SaveInProgress &e ) {
      error_flag = true;
      send_hs( stderr, "Federate::subscribe_attributes():%d SaveInProgress: MOM Object Attributed Subscribe FAILED!%c",
               __LINE__, THLA_NEWLINE );
   } catch ( RTI1516_NAMESPACE::RestoreInProgress &e ) {
      error_flag = true;
      send_hs( stderr, "Federate::subscribe_attributes():%d RestoreInProgress: MOM Object Attributed Subscribe FAILED!%c",
               __LINE__, THLA_NEWLINE );
   } catch ( RTI1516_NAMESPACE::InvalidUpdateRateDesignator &e ) {
      error_flag = true;
      send_hs( stderr, "Federate::subscribe_attributes():%d InvalidUpdateRateDesignator: MOM Object Attributed Subscribe FAILED!!%c",
               __LINE__, THLA_NEWLINE );
   } catch ( RTI1516_NAMESPACE::NotConnected &e ) {
      error_flag = true;
      send_hs( stderr, "Federate::subscribe_attributes():%d NotConnected: MOM Object Attributed Subscribe FAILED!%c",
               __LINE__, THLA_NEWLINE );
   } catch ( RTI1516_NAMESPACE::RTIinternalError &e ) {
      error_flag = true;
      send_hs( stderr, "Federate::subscribe_attributes():%d RTIinternalError: MOM Object Attributed Subscribe FAILED!%c",
               __LINE__, THLA_NEWLINE );
   }

   // Macro to restore the saved FPU Control Word register value.
   TRICKHLA_RESTORE_FPU_CONTROL_WORD;
   TRICKHLA_VALIDATE_FPU_CONTROL_WORD;

   if ( error_flag ) {
      exec_terminate( __FILE__, "Federate::subscribe_attributes() ERROR Detected!" );
   }
}

void Federate::unsubscribe_attributes(
   ObjectClassHandle         class_handle,
   AttributeHandleSet const &attribute_list )
{
   if ( should_print( DEBUG_LEVEL_4_TRACE, DEBUG_SOURCE_FEDERATE ) ) {
      send_hs( stdout, "Federate::unsubscribe_attributes():%d%c",
               __LINE__, THLA_NEWLINE );
   }

   // Macro to save the FPU Control Word register value.
   TRICKHLA_SAVE_FPU_CONTROL_WORD;

   bool error_flag = false;

   try {
      RTI_ambassador->unsubscribeObjectClassAttributes( class_handle, attribute_list );
   } catch ( RTI1516_NAMESPACE::ObjectClassNotDefined &e ) {
      error_flag = true;
      send_hs( stderr, "Federate::unsubscribe_attributes():%d ObjectClassNotDefined: MOM Object Attributed Subscribe FAILED!%c",
               __LINE__, THLA_NEWLINE );
   } catch ( RTI1516_NAMESPACE::AttributeNotDefined &e ) {
      error_flag = true;
      send_hs( stderr, "Federate::unsubscribe_attributes():%d AttributeNotDefined: MOM Object Attributed Subscribe FAILED!%c",
               __LINE__, THLA_NEWLINE );
   } catch ( RTI1516_NAMESPACE::FederateNotExecutionMember &e ) {
      error_flag = true;
      send_hs( stderr, "Federate::unsubscribe_attributes():%d FederateNotExecutionMember: MOM Object Attributed Subscribe FAILED!%c",
               __LINE__, THLA_NEWLINE );
   } catch ( RTI1516_NAMESPACE::SaveInProgress &e ) {
      error_flag = true;
      send_hs( stderr, "Federate::unsubscribe_attributes():%d SaveInProgress: MOM Object Attributed Subscribe FAILED!%c",
               __LINE__, THLA_NEWLINE );
   } catch ( RTI1516_NAMESPACE::RestoreInProgress &e ) {
      error_flag = true;
      send_hs( stderr, "Federate::unsubscribe_attributes():%d RestoreInProgress: MOM Object Attributed Subscribe FAILED!%c",
               __LINE__, THLA_NEWLINE );
   } catch ( RTI1516_NAMESPACE::NotConnected &e ) {
      error_flag = true;
      send_hs( stderr, "Federate::unsubscribe_attributes():%d NotConnected: MOM Object Attributed Subscribe FAILED!%c",
               __LINE__, THLA_NEWLINE );
   } catch ( RTI1516_NAMESPACE::RTIinternalError &e ) {
      error_flag = true;
      send_hs( stderr, "Federate::unsubscribe_attributes():%d RTIinternalError: MOM Object Attributed Subscribe FAILED!%c",
               __LINE__, THLA_NEWLINE );
   }

   // Macro to restore the saved FPU Control Word register value.
   TRICKHLA_RESTORE_FPU_CONTROL_WORD;
   TRICKHLA_VALIDATE_FPU_CONTROL_WORD;

   if ( error_flag ) {
      exec_terminate( __FILE__, "Federate::unsubscribe_attributes() ERROR Detected!" );
   }
}

void Federate::request_attribute_update(
   ObjectClassHandle         class_handle,
   AttributeHandleSet const &attribute_list )
{
   if ( should_print( DEBUG_LEVEL_4_TRACE, DEBUG_SOURCE_FEDERATE ) ) {
      send_hs( stdout, "Federate::request_attribute_update():%d%c",
               __LINE__, THLA_NEWLINE );
   }

   // Macro to save the FPU Control Word register value.
   TRICKHLA_SAVE_FPU_CONTROL_WORD;

   bool error_flag = false;

   try {
      // Request initial values.
      RTI_ambassador->requestAttributeValueUpdate( class_handle,
                                                   attribute_list,
                                                   RTI1516_USERDATA( 0, 0 ) );
   } catch ( ObjectClassNotDefined &e ) {
      error_flag = true;
      send_hs( stderr, "Federate::request_attribute_update():%d ObjectClassNotDefined: Attribute update request FAILED!%c",
               __LINE__, THLA_NEWLINE );
   } catch ( AttributeNotDefined &e ) {
      error_flag = true;
      send_hs( stderr, "Federate::request_attribute_update():%d AttributeNotDefined: Attribute update request FAILED!%c",
               __LINE__, THLA_NEWLINE );
   } catch ( FederateNotExecutionMember &e ) {
      error_flag = true;
      send_hs( stderr, "Federate::request_attribute_update():%d FederateNotExecutionMember: Attribute update request FAILED!%c",
               __LINE__, THLA_NEWLINE );
   } catch ( SaveInProgress &e ) {
      error_flag = true;
      send_hs( stderr, "Federate::request_attribute_update():%d SaveInProgress: Attribute update request FAILED!%c",
               __LINE__, THLA_NEWLINE );
   } catch ( RestoreInProgress &e ) {
      error_flag = true;
      send_hs( stderr, "Federate::request_attribute_update():%d RestoreInProgress: Attribute update request FAILED!%c",
               __LINE__, THLA_NEWLINE );
   } catch ( NotConnected &e ) {
      error_flag = true;
      send_hs( stderr, "Federate::request_attribute_update():%d NotConnected: Attribute update request FAILED!%c",
               __LINE__, THLA_NEWLINE );
   } catch ( RTIinternalError &e ) {
      error_flag = true;
      send_hs( stderr, "Federate::request_attribute_update():%d RTIinternalError: MOM Object Attributed update request FAILED!%c",
               __LINE__, THLA_NEWLINE );
   }

   // Macro to restore the saved FPU Control Word register value.
   TRICKHLA_RESTORE_FPU_CONTROL_WORD;
   TRICKHLA_VALIDATE_FPU_CONTROL_WORD;

   if ( error_flag ) {
      exec_terminate( __FILE__, "Federate::request_attribute_update() ERROR Detected!" );
   }
}

void Federate::ask_MOM_for_federate_names()
{
   if ( should_print( DEBUG_LEVEL_4_TRACE, DEBUG_SOURCE_FEDERATE ) ) {
      send_hs( stdout, "Federate::ask_MOM_for_federate_names():%d%c",
               __LINE__, THLA_NEWLINE );
   }

   // Make sure the MOM handles get initialized before we try to use them.
   if ( !MOM_HLAfederateName_handle.isValid() ) {
      initialize_MOM_handles();
   }

   AttributeHandleSet fedMomAttributes;
   fedMomAttributes.insert( MOM_HLAfederateName_handle );
   fedMomAttributes.insert( MOM_HLAfederate_handle );
   subscribe_attributes( MOM_HLAfederate_class_handle, fedMomAttributes );

   AttributeHandleSet requestedAttributes;
   requestedAttributes.insert( MOM_HLAfederateName_handle );
   requestedAttributes.insert( MOM_HLAfederate_handle );
   request_attribute_update( MOM_HLAfederate_class_handle, requestedAttributes );
}

void Federate::unsubscribe_all_HLAfederate_class_attributes_from_MOM()
{
   if ( should_print( DEBUG_LEVEL_4_TRACE, DEBUG_SOURCE_FEDERATE ) ) {
      send_hs( stdout, "Federate::unsubscribe_all_HLAfederate_class_attributes_from_MOM():%d%c",
               __LINE__, THLA_NEWLINE );
   }

   // Macro to save the FPU Control Word register value.
   TRICKHLA_SAVE_FPU_CONTROL_WORD;

   try {
      // We are done with the MOM interface to unsubscribe from all the
      // class attributes.
      RTI_ambassador->unsubscribeObjectClass( MOM_HLAfederate_class_handle );
   } catch ( ObjectClassNotDefined &e ) {
      send_hs( stderr, "Federate::unsubscribe_all_HLAfederate_class_attributes_from_MOM():%d ObjectClassNotDefined: Unsubscribe object class FAILED!%c",
               __LINE__, THLA_NEWLINE );
   } catch ( FederateNotExecutionMember &e ) {
      send_hs( stderr, "Federate::unsubscribe_all_HLAfederate_class_attributes_from_MOM():%d FederateNotExecutionMember: Unsubscribe object class FAILED!%c",
               __LINE__, THLA_NEWLINE );
   } catch ( SaveInProgress &e ) {
      send_hs( stderr, "Federate::unsubscribe_all_HLAfederate_class_attributes_from_MOM():%d SaveInProgress: Unsubscribe object class FAILED!%c",
               __LINE__, THLA_NEWLINE );
   } catch ( RestoreInProgress &e ) {
      send_hs( stderr, "Federate::unsubscribe_all_HLAfederate_class_attributes_from_MOM():%d RestoreInProgress: Unsubscribe object class FAILED!%c",
               __LINE__, THLA_NEWLINE );
   } catch ( NotConnected &e ) {
      send_hs( stderr, "Federate::unsubscribe_all_HLAfederate_class_attributes_from_MOM():%d NotConnected: Unsubscribe object class FAILED!%c",
               __LINE__, THLA_NEWLINE );
   } catch ( RTIinternalError &e ) {
      send_hs( stderr, "Federate::unsubscribe_all_HLAfederate_class_attributes_from_MOM():%d RTIinternalError: Unsubscribe object class FAILED!%c",
               __LINE__, THLA_NEWLINE );
   }

   // Macro to restore the saved FPU Control Word register value.
   TRICKHLA_RESTORE_FPU_CONTROL_WORD;
   TRICKHLA_VALIDATE_FPU_CONTROL_WORD;
}

void Federate::unsubscribe_all_HLAfederation_class_attributes_from_MOM()
{
   if ( should_print( DEBUG_LEVEL_4_TRACE, DEBUG_SOURCE_FEDERATE ) ) {
      send_hs( stdout, "Federate::unsubscribe_all_HLAfederation_class_attributes_from_MOM():%d%c",
               __LINE__, THLA_NEWLINE );
   }

   // Macro to save the FPU Control Word register value.
   TRICKHLA_SAVE_FPU_CONTROL_WORD;

   try {
      // We are done with the MOM interface so unsubscribe from the class we used.
      RTI_ambassador->unsubscribeObjectClass( MOM_HLAfederation_class_handle );
   } catch ( ObjectClassNotDefined &e ) {
      send_hs( stderr, "Federate::unsubscribe_all_HLAfederation_class_attributes_from_MOM():%d ObjectClassNotDefined: Unsubscribe object class FAILED!%c",
               __LINE__, THLA_NEWLINE );
   } catch ( FederateNotExecutionMember &e ) {
      send_hs( stderr, "Federate::unsubscribe_all_HLAfederation_class_attributes_from_MOM():%d FederateNotExecutionMember: Unsubscribe object class FAILED!%c",
               __LINE__, THLA_NEWLINE );
   } catch ( SaveInProgress &e ) {
      send_hs( stderr, "Federate::unsubscribe_all_HLAfederation_class_attributes_from_MOM():%d SaveInProgress: Unsubscribe object class FAILED!%c",
               __LINE__, THLA_NEWLINE );
   } catch ( RestoreInProgress &e ) {
      send_hs( stderr, "Federate::unsubscribe_all_HLAfederation_class_attributes_from_MOM():%d RestoreInProgress: Unsubscribe object class FAILED!%c",
               __LINE__, THLA_NEWLINE );
   } catch ( NotConnected &e ) {
      send_hs( stderr, "Federate::unsubscribe_all_HLAfederation_class_attributes_from_MOM():%d NotConnected: Unsubscribe object class FAILED!%c",
               __LINE__, THLA_NEWLINE );
   } catch ( RTIinternalError &e ) {
      send_hs( stderr, "Federate::unsubscribe_all_HLAfederation_class_attributes_from_MOM():%d RTIinternalError: Unsubscribe object class FAILED!%c",
               __LINE__, THLA_NEWLINE );
   }

   // Macro to restore the saved FPU Control Word register value.
   TRICKHLA_RESTORE_FPU_CONTROL_WORD;
   TRICKHLA_VALIDATE_FPU_CONTROL_WORD;
}

void Federate::publish_interaction_class(
   RTI1516_NAMESPACE::InteractionClassHandle class_handle )
{
   if ( should_print( DEBUG_LEVEL_4_TRACE, DEBUG_SOURCE_FEDERATE ) ) {
      send_hs( stdout, "Federate::publish_interaction_class():%d%c",
               __LINE__, THLA_NEWLINE );
   }

   // Macro to save the FPU Control Word register value.
   TRICKHLA_SAVE_FPU_CONTROL_WORD;

   try {
      RTI_ambassador->publishInteractionClass( class_handle );
   } catch ( InteractionClassNotDefined &e ) {
      send_hs( stderr, "Federate::publish_interaction_class():%d InteractionClassNotDefined: Publish interaction class FAILED!%c",
               __LINE__, THLA_NEWLINE );
   } catch ( FederateNotExecutionMember &e ) {
      send_hs( stderr, "Federate::publish_interaction_class():%d FederateNotExecutionMember: Publish interaction class FAILED!%c",
               __LINE__, THLA_NEWLINE );
   } catch ( SaveInProgress &e ) {
      send_hs( stderr, "Federate::publish_interaction_class():%d SaveInProgress: Publish interaction class FAILED!%c",
               __LINE__, THLA_NEWLINE );
   } catch ( RestoreInProgress &e ) {
      send_hs( stderr, "Federate::publish_interaction_class():%d RestoreInProgress: Publish interaction class FAILED!%c",
               __LINE__, THLA_NEWLINE );
   } catch ( NotConnected &e ) {
      send_hs( stderr, "Federate::publish_interaction_class():%d NotConnected: Publish interaction class FAILED!%c",
               __LINE__, THLA_NEWLINE );
   } catch ( RTIinternalError &e ) {
      send_hs( stderr, "Federate::publish_interaction_class():%d RTIinternalError: Publish interaction class FAILED!%c",
               __LINE__, THLA_NEWLINE );
   }

   // Macro to restore the saved FPU Control Word register value.
   TRICKHLA_RESTORE_FPU_CONTROL_WORD;
   TRICKHLA_VALIDATE_FPU_CONTROL_WORD;
}

void Federate::unpublish_interaction_class(
   RTI1516_NAMESPACE::InteractionClassHandle class_handle )
{
   if ( should_print( DEBUG_LEVEL_4_TRACE, DEBUG_SOURCE_FEDERATE ) ) {
      send_hs( stdout, "Federate::unpublish_interaction_class():%d%c",
               __LINE__, THLA_NEWLINE );
   }

   // Macro to save the FPU Control Word register value.
   TRICKHLA_SAVE_FPU_CONTROL_WORD;

   try {
      RTI_ambassador->unpublishInteractionClass( class_handle );
   } catch ( InteractionClassNotDefined &e ) {
      send_hs( stderr, "Federate::unpublish_interaction_class():%d InteractionClassNotDefined: Unpublish interaction class FAILED!%c",
               __LINE__, THLA_NEWLINE );
   } catch ( FederateNotExecutionMember &e ) {
      send_hs( stderr, "Federate::unpublish_interaction_class():%d FederateNotExecutionMember: Unpublish interaction class FAILED!%c",
               __LINE__, THLA_NEWLINE );
   } catch ( SaveInProgress &e ) {
      send_hs( stderr, "Federate::unpublish_interaction_class():%d SaveInProgress: Unpublish interaction class FAILED!%c",
               __LINE__, THLA_NEWLINE );
   } catch ( RestoreInProgress &e ) {
      send_hs( stderr, "Federate::unpublish_interaction_class():%d RestoreInProgress: Unpublish interaction class FAILED!%c",
               __LINE__, THLA_NEWLINE );
   } catch ( NotConnected &e ) {
      send_hs( stderr, "Federate::unpublish_interaction_class():%d NotConnected: Unpublish interaction class FAILED!%c",
               __LINE__, THLA_NEWLINE );
   } catch ( RTIinternalError &e ) {
      send_hs( stderr, "Federate::unpublish_interaction_class():%d RTIinternalError: Unpublish interaction class FAILED!%c",
               __LINE__, THLA_NEWLINE );
   }

   // Macro to restore the saved FPU Control Word register value.
   TRICKHLA_RESTORE_FPU_CONTROL_WORD;
   TRICKHLA_VALIDATE_FPU_CONTROL_WORD;
}

void Federate::send_interaction(
   RTI1516_NAMESPACE::InteractionClassHandle         class_handle,
   RTI1516_NAMESPACE::ParameterHandleValueMap const &parameter_list )
{
   // Macro to save the FPU Control Word register value.
   TRICKHLA_SAVE_FPU_CONTROL_WORD;

   bool error_flag = false;
   try {
      (void)RTI_ambassador->sendInteraction( class_handle, parameter_list, RTI1516_USERDATA( 0, 0 ) );
   } catch ( InteractionClassNotPublished &e ) {
      error_flag = true;
      send_hs( stderr, "Federate::send_interaction():%d InteractionClassNotPublished: Send interaction FAILED!%c",
               __LINE__, THLA_NEWLINE );
   } catch ( InteractionParameterNotDefined &e ) {
      error_flag = true;
      send_hs( stderr, "Federate::send_interaction():%d InteractionParameterNotDefined: Send interaction FAILED!%c",
               __LINE__, THLA_NEWLINE );
   } catch ( InteractionClassNotDefined &e ) {
      error_flag = true;
      send_hs( stderr, "Federate::send_interaction():%d InteractionClassNotDefined: Send interaction FAILED!%c",
               __LINE__, THLA_NEWLINE );
   } catch ( SaveInProgress &e ) {
      error_flag = true;
      send_hs( stderr, "Federate::send_interaction():%d SaveInProgress: Send interaction FAILED!%c",
               __LINE__, THLA_NEWLINE );
   } catch ( RestoreInProgress &e ) {
      error_flag = true;
      send_hs( stderr, "Federate::send_interaction():%d RestoreInProgress: Send interaction FAILED!%c",
               __LINE__, THLA_NEWLINE );
   } catch ( FederateNotExecutionMember &e ) {
      error_flag = true;
      send_hs( stderr, "Federate::send_interaction():%d FederateNotExecutionMember: Send interaction FAILED!%c",
               __LINE__, THLA_NEWLINE );
   } catch ( NotConnected &e ) {
      error_flag = true;
      send_hs( stderr, "Federate::send_interaction():%d NotConnected: Send interaction FAILED!%c",
               __LINE__, THLA_NEWLINE );
   } catch ( RTIinternalError &e ) {
      error_flag = true;
      send_hs( stderr, "Federate::send_interaction():%d RTIinternalError: Send interaction FAILED!%c",
               __LINE__, THLA_NEWLINE );
   }

   // Macro to restore the saved FPU Control Word register value.
   TRICKHLA_RESTORE_FPU_CONTROL_WORD;
   TRICKHLA_VALIDATE_FPU_CONTROL_WORD;

   if ( error_flag ) {
      exec_terminate( __FILE__, "Federate::send_interaction() ERROR Detected!" );
   }
}

void Federate::register_generic_sync_point(
   wstring const &label,
   double         time )
{
   // Macro to save the FPU Control Word register value.
   TRICKHLA_SAVE_FPU_CONTROL_WORD;

   // Register the sync-point label.
   try {
      if ( time < 0.0 ) {
         // no time specified
         RTI_ambassador->registerFederationSynchronizationPoint( label, RTI1516_USERDATA( 0, 0 ) );
      } else {
         //DANNY2.7 convert time to microseconds and encode in a buffer to send to RTI
         int64_t       _value = Int64Interval::toMicroseconds( time );
         unsigned char buf[8];
         buf[0] = (unsigned char)( ( _value >> 56 ) & 0xFF );
         buf[1] = (unsigned char)( ( _value >> 48 ) & 0xFF );
         buf[2] = (unsigned char)( ( _value >> 40 ) & 0xFF );
         buf[3] = (unsigned char)( ( _value >> 32 ) & 0xFF );
         buf[4] = (unsigned char)( ( _value >> 24 ) & 0xFF );
         buf[5] = (unsigned char)( ( _value >> 16 ) & 0xFF );
         buf[6] = (unsigned char)( ( _value >> 8 ) & 0xFF );
         buf[7] = (unsigned char)( ( _value >> 0 ) & 0xFF );
         RTI_ambassador->registerFederationSynchronizationPoint( label, RTI1516_USERDATA( buf, 8 ) );
      }

      if ( should_print( DEBUG_LEVEL_2_TRACE, DEBUG_SOURCE_FEDERATE ) ) {
         send_hs( stderr, "Federate::register_generic_sync_point():%d Registered '%ls' synchronization point with RTI.%c",
                  __LINE__, label.c_str(), THLA_NEWLINE );
      }
   } catch ( SaveInProgress &e ) {
      send_hs( stderr, "Federate::register_generic_sync_point():%d EXCPETION: SaveInProgress: Failed to register '%ls' synchronization point with RTI!%c",
               __LINE__, label.c_str(), THLA_NEWLINE );
   } catch ( RestoreInProgress &e ) {
      send_hs( stderr, "Federate::register_generic_sync_point():%d EXCPETION: RestoreInProgress: Failed to register '%ls' synchronization point with RTI!%c",
               __LINE__, label.c_str(), THLA_NEWLINE );
   } catch ( FederateNotExecutionMember &e ) {
      send_hs( stderr, "Federate::register_generic_sync_point():%d EXCPETION: FederateNotExecutionMember: Failed to register '%ls' synchronization point with RTI!%c",
               __LINE__, label.c_str(), THLA_NEWLINE );
   } catch ( NotConnected &e ) {
      send_hs( stderr, "Federate::register_generic_sync_point():%d EXCPETION: NotConnected: Failed to register '%ls' synchronization point with RTI!%c",
               __LINE__, label.c_str(), THLA_NEWLINE );
   } catch ( RTIinternalError &e ) {
      string rti_err_msg;
      StringUtilities::to_string( rti_err_msg, e.what() );
      send_hs( stderr, "Federate::register_generic_sync_point():%d EXCPETION: RTIinternalError '%s': Failed to register '%ls' synchronization point with RTI!%c",
               __LINE__, rti_err_msg.c_str(), label.c_str(), THLA_NEWLINE );
   } catch ( RTI1516_EXCEPTION &e ) {
      send_hs( stderr, "Federate::register_generic_sync_point():%d ERROR: Failed to register '%ls' synchronization point with RTI!%c",
               __LINE__, label.c_str(), THLA_NEWLINE );
   }

   // Macro to restore the saved FPU Control Word register value.
   TRICKHLA_RESTORE_FPU_CONTROL_WORD;
   TRICKHLA_VALIDATE_FPU_CONTROL_WORD;

   return;
}

void Federate::achieve_and_wait_for_synchronization(
   wstring const &label )
{
   if ( should_print( DEBUG_LEVEL_2_TRACE, DEBUG_SOURCE_FEDERATE ) ) {
      send_hs( stdout, "Federate::achieve_and_wait_for_synchronization():%d Label:'%ls'%c",
               __LINE__, label.c_str(), THLA_NEWLINE );
   }

   try {

      execution_control->achieve_and_wait_for_synchronization( *( RTI_ambassador ), this, label );

   } catch ( RTI1516_NAMESPACE::SynchronizationPointLabelNotAnnounced &e ) {
      string s_label;
      StringUtilities::to_string( s_label, label );
      ostringstream errmsg;
      errmsg << "Federate::achieve_and_wait_for_synchronization():" << __LINE__
             << " Label:'" << s_label << "'"
             << " Exception: SynchronizationPointLabelNotAnnounced" << THLA_ENDL;
      send_hs( stderr, (char *)errmsg.str().c_str() );
      exec_terminate( __FILE__, (char *)errmsg.str().c_str() );
   } catch ( RTI1516_NAMESPACE::FederateNotExecutionMember &e ) {
      string s_label;
      StringUtilities::to_string( s_label, label );
      ostringstream errmsg;
      errmsg << "Federate::achieve_and_wait_for_synchronization():" << __LINE__
             << " Label:'" << s_label << "'"
             << " Exception: FederateNotExecutionMember" << THLA_ENDL;
      send_hs( stderr, (char *)errmsg.str().c_str() );
      exec_terminate( __FILE__, (char *)errmsg.str().c_str() );
   } catch ( RTI1516_NAMESPACE::SaveInProgress &e ) {
      string s_label;
      StringUtilities::to_string( s_label, label );
      ostringstream errmsg;
      errmsg << "Federate::achieve_and_wait_for_synchronization():" << __LINE__
             << " Label:'" << s_label << "'"
             << " Exception: SaveInProgress" << THLA_ENDL;
      send_hs( stderr, (char *)errmsg.str().c_str() );
      exec_terminate( __FILE__, (char *)errmsg.str().c_str() );
   } catch ( RTI1516_NAMESPACE::RestoreInProgress &e ) {
      string s_label;
      StringUtilities::to_string( s_label, label );
      ostringstream errmsg;
      errmsg << "Federate::achieve_and_wait_for_synchronization():" << __LINE__
             << " Label:'" << s_label << "'"
             << " Exception: RestoreInProgress" << THLA_ENDL;
      send_hs( stderr, (char *)errmsg.str().c_str() );
      exec_terminate( __FILE__, (char *)errmsg.str().c_str() );
   } catch ( RTI1516_NAMESPACE::RTIinternalError &e ) {
      string s_label;
      StringUtilities::to_string( s_label, label );
      ostringstream errmsg;
      errmsg << "Federate::achieve_and_wait_for_synchronization():" << __LINE__
             << " Label:'" << s_label << "'"
             << " Exception: RTIinternalError" << THLA_ENDL;
      send_hs( stderr, (char *)errmsg.str().c_str() );
      exec_terminate( __FILE__, (char *)errmsg.str().c_str() );
   } catch ( RTI1516_EXCEPTION &e ) {
      string s_label;
      StringUtilities::to_string( s_label, label );
      string rti_err_msg;
      StringUtilities::to_string( rti_err_msg, e.what() );
      ostringstream errmsg;
      errmsg << "Federate::achieve_and_wait_for_synchronization():" << __LINE__
             << " Label:'" << s_label << "'"
             << " Exception: RTI1516_EXCEPTION " << rti_err_msg << THLA_ENDL;
      send_hs( stderr, (char *)errmsg.str().c_str() );
      exec_terminate( __FILE__, (char *)errmsg.str().c_str() );
   }

   if ( should_print( DEBUG_LEVEL_2_TRACE, DEBUG_SOURCE_FEDERATE ) ) {
      execution_control->print_sync_pnts();
   }
}

void Federate::achieve_synchronization_point(
   wstring const &label )
{
   if ( should_print( DEBUG_LEVEL_2_TRACE, DEBUG_SOURCE_FEDERATE ) ) {
      send_hs( stdout, "Federate::achieve_synchronization_point():%d Label:'%ls'%c",
               __LINE__, label.c_str(), THLA_NEWLINE );
   }

   try {

      RTI_ambassador->synchronizationPointAchieved( label );

   } catch ( RTI1516_NAMESPACE::SynchronizationPointLabelNotAnnounced &e ) {
      string s_label;
      StringUtilities::to_string( s_label, label );
      ostringstream errmsg;
      errmsg << "Federate::achieve_synchronization_point():" << __LINE__
             << " Label:'" << s_label << "'"
             << " Exception: SynchronizationPointLabelNotAnnounced" << THLA_ENDL;
      send_hs( stderr, (char *)errmsg.str().c_str() );
      exec_terminate( __FILE__, (char *)errmsg.str().c_str() );
   } catch ( RTI1516_NAMESPACE::FederateNotExecutionMember &e ) {
      string s_label;
      StringUtilities::to_string( s_label, label );
      ostringstream errmsg;
      errmsg << "Federate::achieve_synchronization_point():" << __LINE__
             << " Label:'" << s_label << "'"
             << " Exception: FederateNotExecutionMember" << THLA_ENDL;
      send_hs( stderr, (char *)errmsg.str().c_str() );
      exec_terminate( __FILE__, (char *)errmsg.str().c_str() );
   } catch ( RTI1516_NAMESPACE::SaveInProgress &e ) {
      string s_label;
      StringUtilities::to_string( s_label, label );
      ostringstream errmsg;
      errmsg << "Federate::achieve_synchronization_point():" << __LINE__
             << " Label:'" << s_label << "'"
             << " Exception: SaveInProgress" << THLA_ENDL;
      send_hs( stderr, (char *)errmsg.str().c_str() );
      exec_terminate( __FILE__, (char *)errmsg.str().c_str() );
   } catch ( RTI1516_NAMESPACE::RestoreInProgress &e ) {
      string s_label;
      StringUtilities::to_string( s_label, label );
      ostringstream errmsg;
      errmsg << "Federate::achieve_synchronization_point():" << __LINE__
             << " Label:'" << s_label << "'"
             << " Exception: RestoreInProgress" << THLA_ENDL;
      send_hs( stderr, (char *)errmsg.str().c_str() );
      exec_terminate( __FILE__, (char *)errmsg.str().c_str() );
   } catch ( RTI1516_NAMESPACE::RTIinternalError &e ) {
      string s_label;
      StringUtilities::to_string( s_label, label );
      ostringstream errmsg;
      errmsg << "Federate::achieve_synchronization_point():" << __LINE__
             << " Label:'" << s_label << "'"
             << " Exception: RTIinternalError" << THLA_ENDL;
      send_hs( stderr, (char *)errmsg.str().c_str() );
      exec_terminate( __FILE__, (char *)errmsg.str().c_str() );
   } catch ( RTI1516_EXCEPTION &e ) {
      string s_label;
      StringUtilities::to_string( s_label, label );
      string rti_err_msg;
      StringUtilities::to_string( rti_err_msg, e.what() );
      ostringstream errmsg;
      errmsg << "Federate::achieve_synchronization_point():" << __LINE__
             << " Label:'" << s_label << "'"
             << " Exception: RTI1516_EXCEPTION " << rti_err_msg << THLA_ENDL;
      send_hs( stderr, (char *)errmsg.str().c_str() );
      exec_terminate( __FILE__, (char *)errmsg.str().c_str() );
   }
}

void Federate::announce_sync_point(
   wstring const &         label,
   RTI1516_USERDATA const &user_supplied_tag )
{

   // Dispatch this to the ExecutionControl process.  It will check for
   // any synchronization points that require special handling.
   execution_control->announce_sync_point( *( RTI_ambassador ), label, user_supplied_tag );

   return;
}

void Federate::sync_point_registration_succeeded(
   wstring const &label )
{
   // Call the ExecutionControl method associated with the TirckHLA::Manager.
   execution_control->sync_point_registration_succeeded( label );

   return;
}

void Federate::sync_point_registration_failed(
   wstring const &label,
   bool           not_unique )
{
   // Call the ExecutionControl method associated with the TirckHLA::Manager.
   execution_control->sync_point_registration_failed( label, not_unique );

   return;
}

void Federate::federation_synchronized(
   wstring const &label )
{

   // Mark the sync-point and synchronized.
   execution_control->mark_synchronized( label );

   return;
}

/*!
 * \par<b>Assumptions and Limitations:</b>
 * - Currently only used with SRFOM initialization schemes.
 *  @job_class{freeze_init}
 */
void Federate::freeze_init()
{

   // Dispatch to the ExecutionControl method.
   execution_control->freeze_init();

   return;
}

/*!
 *  @job_class{end_of_frame}
 */
void Federate::enter_freeze()
{

   // Initiate a federation freeze when a Trick freeze is commanded. (If we're
   // here at time 0, set_exec_freeze_command was called in input file.)
   // Otherwise get out now.
   if ( execution_control->get_sim_time() > 0.0 ) {
      if ( exec_get_exec_command() != FreezeCmd ) {
         return; // Trick freeze has not been commanded.
      }
      if ( freeze_the_federation ) {
         return; // freeze already commanded and we will freeze at top of next frame
      }
   }

   // Dispatch to the ExecutionControl method.
   execution_control->enter_freeze();

   return;
}

/*!
 * \par<b>Assumptions and Limitations:</b>
 * - Currently only used with DIS and IMSIM initialization schemes.
 *  @job_class{unfreeze}
 */
void Federate::exit_freeze()
{
   if ( should_print( DEBUG_LEVEL_4_TRACE, DEBUG_SOURCE_FEDERATE ) ) {
      send_hs( stdout, "Federate::exit_freeze():%d announce_freeze:%s, freeze_federation:%s%c",
               __LINE__, ( announce_freeze ? "Yes" : "No" ),
               ( freeze_the_federation ? "Yes" : "No" ), THLA_NEWLINE );
   }

   // Dispatch to the ExecutionControl method.
   execution_control->exit_freeze();

   freeze_the_federation = false;

   return;
}

/*!
 *  @job_class{freeze}
 */
void Federate::check_freeze()
{
   // Check to see if the ExecutionControl should exit freeze.
   if ( execution_control->check_freeze_exit() ) {
      return;
   }

   SIM_MODE exec_mode = exec_get_mode();
   if ( exec_mode == Initialization ) {
      if ( should_print( DEBUG_LEVEL_4_TRACE, DEBUG_SOURCE_FEDERATE ) ) {
         send_hs( stdout, "Federate::check_freeze():%d Pass first Time.%c",
                  __LINE__, THLA_NEWLINE );
      }
      return;
   }
   // We should only check for freeze if we are in Freeze mode. If we are not
   // in Freeze mode then return to avoid running the code below more than once.
   if ( exec_mode != Freeze ) {
      if ( should_print( DEBUG_LEVEL_4_TRACE, DEBUG_SOURCE_FEDERATE ) ) {
         send_hs( stdout, "Federate::check_freeze():%d not in Freeze mode so returning.%c",
                  __LINE__, THLA_NEWLINE );
      }
      return;
   }

   return;
}

void Federate::un_freeze()
{
   // Let the ExecutionControl process do what it needs to do to un-freeze.
   execution_control->un_freeze();

   exec_run();

   return;
}

/*!
 * \par<b>Assumptions and Limitations:</b>
 * - Currently only used with DIS and IMSIM initialization schemes.
 */
bool Federate::is_HLA_save_and_restore_supported()
{
   // Dispatch to the ExecutionControl mechanism.
   return ( execution_control->is_save_and_restore_supported() );
}

/*!
 *  \par<b>Assumptions and Limitations:</b>
 *  - Currently only used with DIS and IMSIM initialization schemes.
 *  @job_class{freeze}
 */
void Federate::perform_checkpoint()
{
   bool force_checkpoint = false;

   // Just return if HLA save and restore is not supported by the simulation
   // initialization scheme selected by the user.
   if ( !is_HLA_save_and_restore_supported() ) {
      return;
   }

   // Dispatch to the ExecutionControl method.
   force_checkpoint = execution_control->perform_save();

   if ( start_to_save || force_checkpoint ) {
      // if I announced the save, sim control panel was clicked and invokes the checkpoint
      if ( !announce_save ) {
         if ( should_print( DEBUG_LEVEL_2_TRACE, DEBUG_SOURCE_FEDERATE ) ) {
            send_hs( stdout, "Federate::perform_checkpoint():%d Federate Save Started %c",
                     __LINE__, THLA_NEWLINE );
         }
         // Create the filename from the Federation name and the "save-name".
         // Replace all directory characters with an underscore.
         string save_name_str;
         StringUtilities::to_string( save_name_str, save_name );
         str_save_label = string( get_federation_name() ) + "_" + save_name_str;
         for ( unsigned int i = 0; i < str_save_label.length(); i++ ) {
            if ( str_save_label[i] == '/' ) {
               str_save_label[i] = '_';
            }
         }
         checkpoint( str_save_label.c_str() ); // calls setup_checkpoint first
      }
      if ( should_print( DEBUG_LEVEL_2_TRACE, DEBUG_SOURCE_FEDERATE ) ) {
         send_hs( stdout, "Federate::perform_checkpoint():%d Checkpoint Dump Completed.%c",
                  __LINE__, THLA_NEWLINE );
      }

      post_checkpoint();
   }
}

/*!
 *  \par<b>Assumptions and Limitations:</b>
 *  - Currently only used with DIS and IMSIM initialization schemes.
 *  @job_class{checkpoint}
 */
void Federate::setup_checkpoint()
{
   string  save_name_str;
   wstring save_name_ws;

   // Don't do federate save during Init or Exit (this allows "regular" init and shutdown checkpoints)
   if ( ( exec_get_mode() == Initialization ) || ( exec_get_mode() == ExitMode ) ) {
      return;
   }

   // Determine if I am the federate that clicked Dump Chkpnt on sim control panel
   // or I am the federate that called start_federation_save
   announce_save = !start_to_save;

   // Check to see if the save has been initiated in the ExcutionControl process?
   // If not then just return.
   if ( !execution_control->is_save_initiated() ) {
      return;
   }

   if ( should_print( DEBUG_LEVEL_2_TRACE, DEBUG_SOURCE_FEDERATE ) ) {
      send_hs( stdout, "Federate::setup_checkpoint():%d Federate Save Pre-checkpoint %c",
               __LINE__, THLA_NEWLINE );
   }

   // if I announced the save, must initiate federation save
   if ( announce_save ) {
      if ( save_name.length() ) {
         // when user calls start_federation_save, save_name is already set
      } else {
         // when user clicks Dump Chkpnt, we need to set the save_name here
         string trick_filename, slash( "/" );
         size_t found;
         // get checkpoint file name specified in control panel
         trick_filename = checkpoint_get_output_file();

         // Trick filename contains dir/filename,
         // need to prepend federation name to filename entered in sim control panel popup
         found = trick_filename.rfind( slash );
         if ( found != string::npos ) {
            save_name_str         = trick_filename.substr( found + 1 ); // filename
            size_t federation_len = strlen( get_federation_name() );
            if ( save_name_str.compare( 0, federation_len, get_federation_name() ) != 0 ) {
               trick_filename.replace( found, slash.length(),
                                       slash + string( get_federation_name() ) + "_" ); // dir/federation_filename
            } else {
               // if it already has federation name prepended, output_file name is good to go
               // but remove it from save_name_str so our str_save_label setting below is correct
               save_name_str = trick_filename.substr( found + 1 + federation_len + 1 ); // filename
            }
         } else {
            save_name_str = trick_filename;
         }

         // TODO: Clean this up later.
         // Set the checkpoint restart files name.
         the_cpr->output_file = trick_filename;

         str_save_label = string( get_federation_name() ) + "_" + save_name_str; // federation_filename
         // set the federate save_name to filename (without the federation name)- this gets announced to other feds
         StringUtilities::to_wstring( save_name_ws, save_name_str );
         set_save_name( save_name_ws );
      } // end set save_name

      // don't request a save if another federate has already requested one
      if ( initiate_save_flag ) {
         request_federation_save_status(); // initiate_save_flag becomes false if another save is occurring
         wait_for_save_status_to_complete();

         request_federation_save();

         unsigned int sleep_micros = 1000;
         unsigned int wait_count   = 0;
         unsigned int wait_check   = 10000000 / sleep_micros; // Number of wait cycles for 10 seconds

         // need to wait for federation to initiate save
         while ( !start_to_save ) {
            usleep( sleep_micros );

            if ( ( !start_to_save ) && ( ( ++wait_count % wait_check ) == 0 ) ) {
               wait_count = 0;
               if ( !is_execution_member() ) {
                  ostringstream errmsg;
                  errmsg << "Federate::setup_checkpoint():" << __LINE__
                         << " Unexpectedly the Federate is no longer an execution"
                         << " member. This means we are either not connected to the"
                         << " RTI or we are no longer joined to the federation"
                         << " execution because someone forced our resignation at"
                         << " the Central RTI Component (CRC) level!"
                         << THLA_ENDL;
                  send_hs( stderr, (char *)errmsg.str().c_str() );
                  exec_terminate( __FILE__, (char *)errmsg.str().c_str() );
               }
            }
         }
         initiate_save_flag = false;
      } else {
         send_hs( stdout, "Federate::setup_checkpoint():%d Federation Save is already in progress! %c",
                  __LINE__, THLA_NEWLINE );
         return;
      }
   }

   // Macro to save the FPU Control Word register value.
   TRICKHLA_SAVE_FPU_CONTROL_WORD;
   try {
      RTI_ambassador->federateSaveBegun();
   } catch ( SaveNotInitiated &e ) {
      send_hs( stderr, "Federate::setup_checkpoint():%d EXCEPTION: SaveNotInitiated%c",
               __LINE__, THLA_NEWLINE );
   } catch ( FederateNotExecutionMember &e ) {
      send_hs( stderr, "Federate::setup_checkpoint():%d EXCEPTION: FederateNotExecutionMember%c",
               __LINE__, THLA_NEWLINE );
   } catch ( RestoreInProgress &e ) {
      send_hs( stderr, "Federate::setup_checkpoint():%d EXCEPTION: RestoreInProgress%c",
               __LINE__, THLA_NEWLINE );
   } catch ( NotConnected &e ) {
      send_hs( stderr, "Federate::setup_checkpoint():%d EXCEPTION: NotConnected%c",
               __LINE__, THLA_NEWLINE );
   } catch ( RTIinternalError &e ) {
      string rti_err_msg;
      StringUtilities::to_string( rti_err_msg, e.what() );
      send_hs( stderr, "Federate::setup_checkpoint():%d EXCEPTION: RTIinternalError: '%s'%c",
               __LINE__, rti_err_msg.c_str(), THLA_NEWLINE );
   }
   // Macro to restore the saved FPU Control Word register value.
   TRICKHLA_RESTORE_FPU_CONTROL_WORD;
   TRICKHLA_VALIDATE_FPU_CONTROL_WORD;

   // This is a shortcut so that we can enforce that only these federates exist
   // when we restore
   write_running_feds_file( (char *)str_save_label.c_str() );

   // Tell the manager to setup the checkpoint data structures.
   manager->setup_checkpoint();

   // Save any synchronization points.
   convert_sync_pts();

   return;
}

/*!
 *  \par<b>Assumptions and Limitations:</b>
 *  - Currently only used with DIS and IMSIM initialization schemes.
 *  @job_class{post_checkpoint}
 */
void Federate::post_checkpoint()
{
   // Just return if HLA save and restore is not supported by the simulation
   // initialization scheme selected by the user.
   if ( !is_HLA_save_and_restore_supported() ) {
      return;
   }

   if ( start_to_save ) {

      // Macro to save the FPU Control Word register value.
      TRICKHLA_SAVE_FPU_CONTROL_WORD;
      try {
         RTI_ambassador->federateSaveComplete();
         if ( should_print( DEBUG_LEVEL_2_TRACE, DEBUG_SOURCE_FEDERATE ) ) {
            send_hs( stdout, "Federate::post_checkpoint():%d Federate Save Completed.%c",
                     __LINE__, THLA_NEWLINE );
         }
         start_to_save = false;
      } catch ( FederateHasNotBegunSave &e ) {
         send_hs( stderr, "Federate::post_checkpoint():%d EXCEPTION: FederateHasNotBegunSave%c",
                  __LINE__, THLA_NEWLINE );
      } catch ( FederateNotExecutionMember &e ) {
         send_hs( stderr, "Federate::post_checkpoint():%d EXCEPTION: FederateNotExecutionMember%c",
                  __LINE__, THLA_NEWLINE );
      } catch ( RestoreInProgress &e ) {
         send_hs( stderr, "Federate::post_checkpoint():%d EXCEPTION: RestoreInProgress%c",
                  __LINE__, THLA_NEWLINE );
      } catch ( NotConnected &e ) {
         send_hs( stderr, "Federate::post_checkpoint():%d EXCEPTION: NotConnected%c",
                  __LINE__, THLA_NEWLINE );
      } catch ( RTIinternalError &e ) {
         string rti_err_msg;
         StringUtilities::to_string( rti_err_msg, e.what() );
         send_hs( stderr, "Federate::post_checkpoint():%d EXCEPTION: RTIinternalError: '%s'%c",
                  __LINE__, rti_err_msg.c_str(), THLA_NEWLINE );
      }
      // Macro to restore the saved FPU Control Word register value.
      TRICKHLA_RESTORE_FPU_CONTROL_WORD;
      TRICKHLA_VALIDATE_FPU_CONTROL_WORD;
   } else {
      if ( should_print( DEBUG_LEVEL_2_TRACE, DEBUG_SOURCE_FEDERATE ) ) {
         send_hs( stdout, "Federate::post_checkpoint():%d Federate Save Already Completed.%c",
                  __LINE__, THLA_NEWLINE );
      }
   }
}

/*!
 *  \par<b>Assumptions and Limitations:</b>
 *  - Currently only used with DIS and IMSIM initialization schemes.
 *  @job_class{freeze}
 */
void Federate::perform_restore()
{
   // Just return if HLA save and restore is not supported by the simulation
   // initialization scheme selected by the user.
   if ( !is_HLA_save_and_restore_supported() ) {
      return;
   }

   if ( start_to_restore ) {
      // if I announced the restore, sim control panel was clicked and invokes the load
      if ( !announce_restore ) {
         if ( should_print( DEBUG_LEVEL_2_TRACE, DEBUG_SOURCE_FEDERATE ) ) {
            send_hs( stdout, "Federate::perform_restore():%d Federate Restore Started.%c",
                     __LINE__, THLA_NEWLINE );
         }
         // Create the filename from the Federation name and the "restore-name".
         // Replace all directory characters with an underscore.
         string restore_name_str;
         StringUtilities::to_string( restore_name_str, restore_name );
         str_restore_label = string( get_federation_name() ) + "_" + restore_name_str;
         for ( unsigned int i = 0; i < str_restore_label.length(); i++ ) {
            if ( str_restore_label[i] == '/' ) {
               str_restore_label[i] = '_';
            }
         }
         send_hs( stdout, "Federate::perform_restore():%d LOADING %s%c",
                  __LINE__, str_restore_label.c_str(), THLA_NEWLINE );
         check_HLA_save_directory(); // make sure we have a save directory specified

         // This will run pre-load-checkpoint jobs, clear memory, read checkpoint file, and run restart jobs
         load_checkpoint( ( string( this->HLA_save_directory ) + "/" + str_restore_label ).c_str() );

         load_checkpoint_job();

         //exec_freeze();
      }

      if ( should_print( DEBUG_LEVEL_2_TRACE, DEBUG_SOURCE_FEDERATE ) ) {
         send_hs( stdout, "Federate::perform_restore():%d Checkpoint Load Completed.%c",
                  __LINE__, THLA_NEWLINE );
      }

      post_restore();
   }
}

/*!
 *  \par<b>Assumptions and Limitations:</b>
 *  - Currently only used with DIS and IMSIM initialization schemes.
 *  @job_class{preload_checkpoint}
 */
void Federate::setup_restore()
{
   // Just return if HLA save and restore is not supported by the simulation
   // initialization scheme selected by the user.
   if ( !is_HLA_save_and_restore_supported() ) {
      return;
   }

   // if restoring at startup, do nothing here (that is handled in restore_checkpoint)
   if ( !is_federate_executing() ) {
      return;
   }

   if ( should_print( DEBUG_LEVEL_2_TRACE, DEBUG_SOURCE_FEDERATE ) ) {
      send_hs( stdout, "Federate::setup_restore():%d Federate Restore Pre-load.%c",
               __LINE__, THLA_NEWLINE );
   }
   // Determine if I am the federate that clicked Load Chkpnt on sim control panel
   announce_restore = !start_to_restore;
   announce_freeze  = announce_restore;

   // if I announced the restore, must initiate federation restore
   if ( announce_restore ) {
      string trick_filename, slash_fedname( "/" + string( get_federation_name() ) + "_" );
      size_t found;

      // Otherwise set restore_name_str using trick's file name
      trick_filename = checkpoint_get_load_file();

      // Trick memory manager load_checkpoint_file_name already contains correct dir/federation_filename
      // (chosen in sim control panel popup) we need just the filename minus the federation name to initiate restore
      found = trick_filename.rfind( slash_fedname );
      string restore_name_str;
      if ( found != string::npos ) {
         restore_name_str = trick_filename.substr( found + slash_fedname.length() ); // filename
      } else {
         restore_name_str = trick_filename;
      }
      str_restore_label = string( get_federation_name() ) + "_" + restore_name_str; // federation_filename
      check_HLA_save_directory();                                                   // make sure we have a save directory specified
      // make sure only the required federates are in the federation before we do the restore
      read_running_feds_file( (char *)str_restore_label.c_str() );
      string tRetString;
      tRetString = wait_for_required_federates_to_join(); // sets running_feds_count
      if ( !tRetString.empty() ) {
         tRetString += THLA_NEWLINE;
         send_hs( stderr, "Federate::setup_restore():%d%c", __LINE__, THLA_NEWLINE );
         send_hs( stderr, (char *)tRetString.c_str() );
         exec_terminate( __FILE__, (char *)tRetString.c_str() );
      }
      // set the federate restore_name to filename (without the federation name)- this gets announced to other feds
      initiate_restore_announce( restore_name_str.c_str() );

      unsigned int sleep_micros = 1000;
      unsigned int wait_count   = 0;
      unsigned int wait_check   = 10000000 / sleep_micros; // Number of wait cycles for 10 seconds

      // need to wait for federation to initiate restore
      while ( !start_to_restore ) {
         usleep( sleep_micros );

         if ( ( !start_to_restore ) && ( ( ++wait_count % wait_check ) == 0 ) ) {
            wait_count = 0;
            if ( !is_execution_member() ) {
               ostringstream errmsg;
               errmsg << "Federate::setup_restore():" << __LINE__
                      << " Unexpectedly the Federate is no longer an execution"
                      << " member. This means we are either not connected to the"
                      << " RTI or we are no longer joined to the federation"
                      << " execution because someone forced our resignation at"
                      << " the Central RTI Component (CRC) level!"
                      << THLA_ENDL;
               send_hs( stderr, (char *)errmsg.str().c_str() );
               exec_terminate( __FILE__, (char *)errmsg.str().c_str() );
            }
         }
      }
   }

   restore_process = Restore_In_Progress;
}

/*!
 *  \par<b>Assumptions and Limitations:</b>
 *  - Currently only used with DIS and IMSIM initialization schemes.
 */
void Federate::post_restore()
{
   // Just return if HLA save and restore is not supported by the simulation
   // initialization scheme selected by the user.
   if ( !is_HLA_save_and_restore_supported() ) {
      return;
   }

   if ( start_to_restore ) {
      restore_process = Restore_Complete;

      // Make a copy of restore_process because it is used in the
      // inform_RTI_of_restore_completion() function.
      // (backward compatibility with previous restore process)
      prev_restore_process = restore_process;

      copy_running_feds_into_known_feds();

      // wait for RTI to inform us that the federation restore has
      // begun before informing the RTI that we are done.
      wait_for_federation_restore_begun();

      // signal RTI that this federate has already been loaded
      inform_RTI_of_restore_completion();

      // wait until we get a callback to inform us that the federation restore is complete
      string tStr = wait_for_federation_restore_to_complete();
      if ( tStr.length() ) {
         char errmsg[80];
         wait_for_federation_restore_failed_callback_to_complete();
         snprintf( errmsg, sizeof( errmsg ), "TrickFederate::post_restore():%d %s %c", __LINE__, tStr.c_str(), THLA_NEWLINE );
         send_hs( stderr, errmsg );
         exec_terminate( __FILE__, errmsg );
      }

      if ( should_print( DEBUG_LEVEL_2_TRACE, DEBUG_SOURCE_FEDERATE ) ) {
         send_hs( stdout, "Federate::post_restore():%d Federation Restore Completed.%c",
                  __LINE__, THLA_NEWLINE );
         send_hs( stdout, "Federate::post_restore():%d Rebuilding HLA Handles.%c",
                  __LINE__, THLA_NEWLINE );
      }

      // get us restarted again...
      // reset RTI data to the state it was in when checkpointed
      manager->reset_mgr_initialized();
      manager->setup_all_ref_attributes();
      manager->setup_all_RTI_handles();
      manager->set_all_object_instance_handles_by_name();

      if ( announce_restore ) {
         set_all_federate_MOM_instance_handles_by_name();
         restore_federate_handles_from_MOM();
      }

      // Restore interactions and sync points
      manager->restore_interactions();
      reinstate_logged_sync_pts();

      // Restore ownership transfer data for all objects
      Object *objects   = manager->get_objects();
      int     obj_count = manager->get_object_count();
      for ( int i = 0; i < obj_count; i++ ) {
         objects[i].restore_ownership_transfer_checkpointed_data();
      }

      // Macro to save the FPU Control Word register value.
      TRICKHLA_SAVE_FPU_CONTROL_WORD;
      try {
         HLAinteger64Time fedTime;
         RTI_ambassador->queryLogicalTime( fedTime );
         set_granted_time( fedTime );
      } catch ( FederateNotExecutionMember &e ) {
         send_hs( stderr, "Federate::post_restore():%d queryLogicalTime EXCEPTION: FederateNotExecutionMember%c",
                  __LINE__, THLA_NEWLINE );
      } catch ( SaveInProgress &e ) {
         send_hs( stderr, "Federate::post_restore():%d queryLogicalTime EXCEPTION: SaveInProgress%c",
                  __LINE__, THLA_NEWLINE );
      } catch ( RestoreInProgress &e ) {
         send_hs( stderr, "Federate::post_restore():%d queryLogicalTime EXCEPTION: RestoreInProgress%c",
                  __LINE__, THLA_NEWLINE );
      } catch ( NotConnected &e ) {
         send_hs( stderr, "Federate::post_restore():%d queryLogicalTime EXCEPTION: NotConnected%c",
                  __LINE__, THLA_NEWLINE );
      } catch ( RTIinternalError &e ) {
         send_hs( stderr, "Federate::post_restore():%d queryLogicalTime EXCEPTION: RTIinternalError%c",
                  __LINE__, THLA_NEWLINE );
      }

      // Macro to restore the saved FPU Control Word register value.
      TRICKHLA_RESTORE_FPU_CONTROL_WORD;
      TRICKHLA_VALIDATE_FPU_CONTROL_WORD;

      HLA_time       = get_granted_time();
      requested_time = granted_time;

      federation_restored();

      if ( should_print( DEBUG_LEVEL_2_TRACE, DEBUG_SOURCE_FEDERATE ) ) {
         send_hs( stdout, "Federate::post_restore():%d Federate Restart Completed.%c",
                  __LINE__, THLA_NEWLINE );
      }
   } else {
      if ( should_print( DEBUG_LEVEL_2_TRACE, DEBUG_SOURCE_FEDERATE ) ) {
         send_hs( stdout, "Federate::post_restore():%d Federate Restore Already Completed.%c",
                  __LINE__, THLA_NEWLINE );
      }
   }
}

void Federate::set_granted_time(
   double time )
{
   granted_time.setTo( time );
}

void Federate::set_granted_time(
   RTI1516_NAMESPACE::LogicalTime const &time )
{
   granted_time.setTo( time );
}

void Federate::set_requested_time(
   double time )
{
   requested_time.setTo( time );
}

void Federate::set_requested_time(
   RTI1516_NAMESPACE::LogicalTime const &time )
{
   requested_time.setTo( time );
}

void Federate::set_lookahead(
   double value )
{
   lookahead.setTo( value );
   lookahead_time = value;
}

void Federate::time_advance_request_to_GALT()
{
   // Simply return if we are the master federate that created the federation,
   // or if time management is not enabled.
   if ( ( !time_management )
        || ( execution_control->is_master()
             && !execution_control->is_late_joiner() ) ) {
      return;
   }

   // Macro to save the FPU Control Word register value.
   TRICKHLA_SAVE_FPU_CONTROL_WORD;

   try {
      HLAinteger64Time fedTime;
      if ( RTI_ambassador->queryGALT( fedTime ) ) {
         int64_t L = lookahead.getTimeInMicros();
         if ( L > 0 ) {
            int64_t GALT = fedTime.getTime();
            // Make sure the time is an integer multiple of the lookahead time.
            fedTime.setTime( ( ( GALT / L ) + 1 ) * L );
         }
         set_requested_time( fedTime );
      }
   } catch ( FederateNotExecutionMember &e ) {
      send_hs( stderr, "Federate::time_advance_request_to_GALT():%d Query-GALT EXCEPTION: FederateNotExecutionMember%c",
               __LINE__, THLA_NEWLINE );
   } catch ( SaveInProgress &e ) {
      send_hs( stderr, "Federate::time_advance_request_to_GALT():%d Query-GALT EXCEPTION: SaveInProgress%c",
               __LINE__, THLA_NEWLINE );
   } catch ( RestoreInProgress &e ) {
      send_hs( stderr, "Federate::time_advance_request_to_GALT():%d Query-GALT EXCEPTION: RestoreInProgress%c",
               __LINE__, THLA_NEWLINE );
   } catch ( NotConnected &e ) {
      send_hs( stderr, "Federate::time_advance_request_to_GALT():%d Query-GALT EXCEPTION: NotConnected%c",
               __LINE__, THLA_NEWLINE );
   } catch ( RTIinternalError &e ) {
      send_hs( stderr, "Federate::time_advance_request_to_GALT():%d Query-GALT EXCEPTION: RTIinternalError%c",
               __LINE__, THLA_NEWLINE );
   }

   // Macro to restore the saved FPU Control Word register value.
   TRICKHLA_RESTORE_FPU_CONTROL_WORD;
   TRICKHLA_VALIDATE_FPU_CONTROL_WORD;

   if ( should_print( DEBUG_LEVEL_3_TRACE, DEBUG_SOURCE_FEDERATE ) ) {
      send_hs( stdout, "Federate::time_advance_request_to_GALT():%d Logical Time:%lf%c",
               __LINE__, requested_time.getDoubleTime(), THLA_NEWLINE );
   }

   // Perform the time-advance request to go to the requested time.
   perform_time_advance_request();
}

void Federate::time_advance_request_to_GALT_LCTS_multiple()
{
   // Simply return if we are the master federate that created the federation,
   // or if time management is not enabled.
   if ( ( !time_management )
        || ( execution_control->is_master()
             && !execution_control->is_late_joiner() ) ) {
      return;
   }

   // Setup the Least-Common-Time-Step time value.
   int64_t LCTS = execution_control->get_least_common_time_step();
   if ( LCTS <= 0 ) {
      // FIXME: EZC - Need to figure out what the correct call is for this.
      LCTS = lookahead.getTimeInMicros();
   }

   // Macro to save the FPU Control Word register value.
   TRICKHLA_SAVE_FPU_CONTROL_WORD;

   try {
      HLAinteger64Time fedTime;
      if ( RTI_ambassador->queryGALT( fedTime ) ) {
         if ( LCTS > 0 ) {
            int64_t GALT = fedTime.getTime();
            // Make sure the time is an integer multiple of the LCTS time.
            fedTime.setTime( ( ( GALT / LCTS ) + 1 ) * LCTS );
         }
         set_requested_time( fedTime );
      }
   } catch ( FederateNotExecutionMember &e ) {
      send_hs( stderr, "Federate::time_advance_request_to_GALT_LCTS_multiple():%d Query-GALT EXCEPTION: FederateNotExecutionMember%c",
               __LINE__, THLA_NEWLINE );
   } catch ( SaveInProgress &e ) {
      send_hs( stderr, "Federate::time_advance_request_to_GALT_LCTS_multiple():%d Query-GALT EXCEPTION: SaveInProgress%c",
               __LINE__, THLA_NEWLINE );
   } catch ( RestoreInProgress &e ) {
      send_hs( stderr, "Federate::time_advance_request_to_GALT_LCTS_multiple():%d Query-GALT EXCEPTION: RestoreInProgress%c",
               __LINE__, THLA_NEWLINE );
   } catch ( NotConnected &e ) {
      send_hs( stderr, "Federate::time_advance_request_to_GALT_LCTS_multiple():%d Query-GALT EXCEPTION: NotConnected%c",
               __LINE__, THLA_NEWLINE );
   } catch ( RTIinternalError &e ) {
      send_hs( stderr, "Federate::time_advance_request_to_GALT_LCTS_multiple():%d Query-GALT EXCEPTION: RTIinternalError%c",
               __LINE__, THLA_NEWLINE );
   }

   // Macro to restore the saved FPU Control Word register value.
   TRICKHLA_RESTORE_FPU_CONTROL_WORD;
   TRICKHLA_VALIDATE_FPU_CONTROL_WORD;

   if ( should_print( DEBUG_LEVEL_3_TRACE, DEBUG_SOURCE_FEDERATE ) ) {
      send_hs( stdout, "Federate::time_advance_request_to_GALT_LCTS_multiple():%d Logical Time:%lf%c",
               __LINE__, requested_time.getDoubleTime(), THLA_NEWLINE );
   }

   // Perform the time-advance request to go to the requested time.
   perform_time_advance_request();
}

/*!
 * @job_class{initialization}
 */
void Federate::create_federation()
{
   // Macro to save the FPU Control Word register value.
   TRICKHLA_SAVE_FPU_CONTROL_WORD;

   // Sanity check.
   if ( RTI_ambassador.get() == NULL ) {
      ostringstream errmsg;
      errmsg << "Federate::create_federation():" << __LINE__
             << " ERROR: NULL pointer to RTIambassador!" << THLA_ENDL;
      send_hs( stderr, (char *)errmsg.str().c_str() );
      exec_terminate( __FILE__, (char *)errmsg.str().c_str() );
   }

   if ( should_print( DEBUG_LEVEL_4_TRACE, DEBUG_SOURCE_FEDERATE ) ) {
      send_hs( stdout, "Federate::create_federation():%d Attempting to create Federation '%s'%c",
               __LINE__, get_federation_name(), THLA_NEWLINE );
   }

   // Create the wide-string version of the federation name.
   wstring federation_name_ws;
   StringUtilities::to_wstring( federation_name_ws, federation_name );

   try {
      this->federation_created_by_federate = false;
      this->federation_exists              = false;

      wstring          MIM_module_ws = L"";
      VectorOfWstrings fomModulesVector;

      // Add the user specified FOM-modules to the vector by parsing the comma
      // separated list of modules.
      if ( FOM_modules != NULL ) {
         StringUtilities::tokenize( FOM_modules, fomModulesVector, "," );
      }

      // Determine if the user specified a MIM-module, which determines how
      // we create the federation execution.
      if ( MIM_module != NULL ) {
         StringUtilities::to_wstring( MIM_module_ws, MIM_module );
      }

      if ( MIM_module_ws.empty() ) {
         // Create the Federation execution.
         RTI_ambassador->createFederationExecution( federation_name_ws,
                                                    fomModulesVector,
                                                    L"HLAinteger64Time" );
      } else {
         // Create the Federation execution with a user specified MIM.
         RTI_ambassador->createFederationExecutionWithMIM( federation_name_ws,
                                                           fomModulesVector,
                                                           MIM_module_ws,
                                                           L"HLAinteger64Time" );
      }

      this->federation_created_by_federate = true;
      this->federation_exists              = true;

      if ( should_print( DEBUG_LEVEL_2_TRACE, DEBUG_SOURCE_FEDERATE ) ) {
         send_hs( stdout, "Federate::create_federation():%d Created Federation '%s'%c",
                  __LINE__, get_federation_name(), THLA_NEWLINE );
      }
   } catch ( RTI1516_NAMESPACE::FederationExecutionAlreadyExists &e ) {
      // Just ignore the exception if the federation execution already exits
      // because of how the multiphase initialization is designed this is not
      // an error since everyone tries to create the federation as the first
      // thing they do.
      this->federation_exists = true;

      if ( should_print( DEBUG_LEVEL_2_TRACE, DEBUG_SOURCE_FEDERATE ) ) {
         send_hs( stdout, "Federate::create_federation():%d Federation already exists for '%s'%c",
                  __LINE__, get_federation_name(), THLA_NEWLINE );
      }
   } catch ( RTI1516_NAMESPACE::CouldNotOpenFDD &e ) {
      // Macro to restore the saved FPU Control Word register value.
      TRICKHLA_RESTORE_FPU_CONTROL_WORD;
      TRICKHLA_VALIDATE_FPU_CONTROL_WORD;

      string rti_err_msg;
      StringUtilities::to_string( rti_err_msg, e.what() );

      ostringstream errmsg;
      errmsg << "Federate::create_federation():" << __LINE__
             << " Could not open FOM-modules: '"
             << FOM_modules << "'";
      if ( MIM_module != NULL ) {
         errmsg << " or MIM-module: '" << MIM_module << "'";
      }
      errmsg << ", RTI Exception: " << rti_err_msg << THLA_ENDL;
      send_hs( stderr, (char *)errmsg.str().c_str() );
      exec_terminate( __FILE__, (char *)errmsg.str().c_str() );
   } catch ( RTI1516_NAMESPACE::ErrorReadingFDD &e ) {
      // Macro to restore the saved FPU Control Word register value.
      TRICKHLA_RESTORE_FPU_CONTROL_WORD;
      TRICKHLA_VALIDATE_FPU_CONTROL_WORD;

      string rti_err_msg;
      StringUtilities::to_string( rti_err_msg, e.what() );

      ostringstream errmsg;
      errmsg << "Federate::create_federation():" << __LINE__
             << " Error reading FOM-modules: '"
             << FOM_modules << "'";
      if ( MIM_module != NULL ) {
         errmsg << " or MIM-module: '" << MIM_module << "'";
      }
      errmsg << ", RTI Exception: " << rti_err_msg << THLA_ENDL;
      send_hs( stderr, (char *)errmsg.str().c_str() );
      exec_terminate( __FILE__, (char *)errmsg.str().c_str() );
   } catch ( RTI1516_NAMESPACE::CouldNotCreateLogicalTimeFactory &e ) {
      // Macro to restore the saved FPU Control Word register value.
      TRICKHLA_RESTORE_FPU_CONTROL_WORD;
      TRICKHLA_VALIDATE_FPU_CONTROL_WORD;

      string rti_err_msg;
      StringUtilities::to_string( rti_err_msg, e.what() );

      ostringstream errmsg;
      errmsg << "Federate::create_federation():" << __LINE__
             << " Could not create logical time factory 'HLAinteger64Time"
             << "', RTI Exception: " << rti_err_msg << "\n  Make sure that you "
             << "are using a IEEE_1516_2010-compliant RTI version which "
             << "supplies the 'HLAinteger64Time' class." << THLA_ENDL;
      send_hs( stderr, (char *)errmsg.str().c_str() );
      exec_terminate( __FILE__, (char *)errmsg.str().c_str() );
   } catch ( RTI1516_NAMESPACE::NotConnected &e ) {
      // Macro to restore the saved FPU Control Word register value.
      TRICKHLA_RESTORE_FPU_CONTROL_WORD;
      TRICKHLA_VALIDATE_FPU_CONTROL_WORD;

      ostringstream errmsg;
      errmsg << "Federate::create_federation():" << __LINE__
             << " EXCEPTION: NotConnected" << THLA_ENDL;
      send_hs( stderr, (char *)errmsg.str().c_str() );
      exec_terminate( __FILE__, (char *)errmsg.str().c_str() );
   } catch ( RTI1516_NAMESPACE::RTIinternalError &e ) {
      // Macro to restore the saved FPU Control Word register value.
      TRICKHLA_RESTORE_FPU_CONTROL_WORD;
      TRICKHLA_VALIDATE_FPU_CONTROL_WORD;

      string rti_err_msg;
      StringUtilities::to_string( rti_err_msg, e.what() );

      ostringstream errmsg;
      errmsg << "Federate::create_federation():" << __LINE__
             << " RTI Internal Error: " << rti_err_msg << THLA_ENDL;
      send_hs( stderr, (char *)errmsg.str().c_str() );
      exec_terminate( __FILE__, (char *)errmsg.str().c_str() );
   } catch ( RTI1516_EXCEPTION &e ) {
      // Macro to restore the saved FPU Control Word register value.
      TRICKHLA_RESTORE_FPU_CONTROL_WORD;
      TRICKHLA_VALIDATE_FPU_CONTROL_WORD;

      string rti_err_msg;
      StringUtilities::to_string( rti_err_msg, e.what() );

      // This is an error so print out an informative message and terminate.
      ostringstream errmsg;
      errmsg << "Federate::create_federation():" << __LINE__
             << " Unrecoverable error in federation '" << get_federation_name()
             << "' creation, RTI Exception: " << rti_err_msg << THLA_ENDL;
      send_hs( stderr, (char *)errmsg.str().c_str() );
      exec_terminate( __FILE__, (char *)errmsg.str().c_str() );
   }
   // Macro to restore the saved FPU Control Word register value.
   TRICKHLA_RESTORE_FPU_CONTROL_WORD;
   TRICKHLA_VALIDATE_FPU_CONTROL_WORD;
}

/*!
 * @job_class{initialization}
 */
void Federate::join_federation(
   const char *const federate_name,
   const char *const federate_type )
{
   // Macro to save the FPU Control Word register value.
   TRICKHLA_SAVE_FPU_CONTROL_WORD;

   // Sanity check.
   if ( RTI_ambassador.get() == NULL ) {
      ostringstream errmsg;
      errmsg << "Federate::join_federation():" << __LINE__
             << " NULL pointer to RTIambassador!" << THLA_ENDL;
      send_hs( stderr, (char *)errmsg.str().c_str() );
      exec_terminate( __FILE__, (char *)errmsg.str().c_str() );
   }
   if ( federate_ambassador == static_cast< FedAmb * >( NULL ) ) {
      ostringstream errmsg;
      errmsg << "Federate::join_federation():" << __LINE__
             << " NULL pointer to FederateAmbassador!" << THLA_ENDL;
      send_hs( stderr, (char *)errmsg.str().c_str() );
      exec_terminate( __FILE__, (char *)errmsg.str().c_str() );
   }
   if ( this->federation_joined ) {
      if ( should_print( DEBUG_LEVEL_2_TRACE, DEBUG_SOURCE_FEDERATE ) ) {
         ostringstream errmsg;
         errmsg << "Federate::join_federation():" << __LINE__
                << " Federation '" << get_federation_name()
                << "': ALREADY JOINED FEDERATION EXECUTION" << THLA_ENDL;
         send_hs( stderr, (char *)errmsg.str().c_str() );
      }
      return;
   }

   // Make sure the federate name has been specified.
   if ( ( federate_name == NULL ) || ( *federate_name == '\0' ) ) {
      ostringstream errmsg;
      errmsg << "Federate::join_federation():" << __LINE__
             << " Unexpected NULL federate name." << THLA_ENDL;
      send_hs( stderr, (char *)errmsg.str().c_str() );
      exec_terminate( __FILE__, (char *)errmsg.str().c_str() );
      return;
   }

   // Create the wide-string version of the federation and federate name & type.
   wstring federation_name_ws;
   StringUtilities::to_wstring( federation_name_ws, federation_name );
   wstring fed_name_ws;
   StringUtilities::to_wstring( fed_name_ws, federate_name );
   wstring fed_type_ws;
   if ( ( federate_type == NULL ) || ( *federate_type == '\0' ) ) {
      // Just set the federate type to the name if it was not specified.
      StringUtilities::to_wstring( fed_type_ws, federate_name );
   } else {
      StringUtilities::to_wstring( fed_type_ws, federate_type );
   }

   // Join the named federation execution as the named federate type.
   // Federate types (2nd argument to joinFederationExecution) does not have
   // to be unique in a federation execution; however, the save/restore
   // services use this information but we are not doing save/restore here
   // so we won't worry about it here (best to make the names
   // unique if you do save/restore unless you understand how save/restore
   // will use the information.
   if ( should_print( DEBUG_LEVEL_4_TRACE, DEBUG_SOURCE_FEDERATE ) ) {
      send_hs( stdout, "Federate::join_federation():%d Attempting to Join Federation '%s'%c",
               __LINE__, get_federation_name(), THLA_NEWLINE );
   }
   try {
      this->federation_joined = false;

      VectorOfWstrings fomModulesVector;

      // Add the user specified FOM-modules to the vector by parsing the comma
      // separated list of modules.
      if ( FOM_modules != NULL ) {
         StringUtilities::tokenize( FOM_modules, fomModulesVector, "," );
      }

      federate_id             = RTI_ambassador->joinFederationExecution( fed_name_ws,
                                                             fed_type_ws,
                                                             federation_name_ws,
                                                             fomModulesVector );
      this->federation_joined = true;

      if ( should_print( DEBUG_LEVEL_2_TRACE, DEBUG_SOURCE_FEDERATE ) ) {
         string id_str;
         StringUtilities::to_string( id_str, federate_id );

         send_hs( stdout, "Federate::join_federation():%d Joined Federation '%s', Federate-Handle:%s%c",
                  __LINE__, get_federation_name(), id_str.c_str(), THLA_NEWLINE );
      }
   } catch ( RTI1516_NAMESPACE::CouldNotCreateLogicalTimeFactory &e ) {
      // Macro to restore the saved FPU Control Word register value.
      TRICKHLA_RESTORE_FPU_CONTROL_WORD;
      TRICKHLA_VALIDATE_FPU_CONTROL_WORD;

      ostringstream errmsg;
      errmsg << "Federate::join_federation():" << __LINE__
             << " EXCEPTION: CouldNotCreateLogicalTimeFactory" << THLA_ENDL;

      send_hs( stderr, (char *)errmsg.str().c_str() );
      exec_terminate( __FILE__, (char *)errmsg.str().c_str() );
   } catch ( RTI1516_NAMESPACE::FederateNameAlreadyInUse &e ) {
      // Macro to restore the saved FPU Control Word register value.
      TRICKHLA_RESTORE_FPU_CONTROL_WORD;
      TRICKHLA_VALIDATE_FPU_CONTROL_WORD;

      ostringstream errmsg;
      errmsg << "Federate::join_federation():" << __LINE__
             << " EXCEPTION: FederateNameAlreadyInUse! Federate name:\""
             << get_federate_name() << "\"" << THLA_ENDL;

      send_hs( stderr, (char *)errmsg.str().c_str() );
      exec_terminate( __FILE__, (char *)errmsg.str().c_str() );
   } catch ( RTI1516_NAMESPACE::InconsistentFDD &e ) {
      // Macro to restore the saved FPU Control Word register value.
      TRICKHLA_RESTORE_FPU_CONTROL_WORD;
      TRICKHLA_VALIDATE_FPU_CONTROL_WORD;

      ostringstream errmsg;
      errmsg << "Federate::join_federation():" << __LINE__
             << " EXCEPTION: InconsistentFDD! FOM-modules:\""
             << FOM_modules << "\"" << THLA_ENDL;

      send_hs( stderr, (char *)errmsg.str().c_str() );
      exec_terminate( __FILE__, (char *)errmsg.str().c_str() );
   } catch ( RTI1516_NAMESPACE::ErrorReadingFDD &e ) {
      // Macro to restore the saved FPU Control Word register value.
      TRICKHLA_RESTORE_FPU_CONTROL_WORD;
      TRICKHLA_VALIDATE_FPU_CONTROL_WORD;

      ostringstream errmsg;
      errmsg << "Federate::join_federation():" << __LINE__
             << " EXCEPTION: ErrorReadingFDD! FOM-modules:\""
             << FOM_modules << "\"" << THLA_ENDL;

      send_hs( stderr, (char *)errmsg.str().c_str() );
      exec_terminate( __FILE__, (char *)errmsg.str().c_str() );
   } catch ( RTI1516_NAMESPACE::CouldNotOpenFDD &e ) {
      // Macro to restore the saved FPU Control Word register value.
      TRICKHLA_RESTORE_FPU_CONTROL_WORD;
      TRICKHLA_VALIDATE_FPU_CONTROL_WORD;

      ostringstream errmsg;
      errmsg << "Federate::join_federation():" << __LINE__
             << " EXCEPTION: CouldNotOpenFDD! FOM-modules:\""
             << FOM_modules << "\"" << THLA_ENDL;

      send_hs( stderr, (char *)errmsg.str().c_str() );
      exec_terminate( __FILE__, (char *)errmsg.str().c_str() );
   } catch ( RTI1516_NAMESPACE::FederateAlreadyExecutionMember &e ) {
      // Macro to restore the saved FPU Control Word register value.
      TRICKHLA_RESTORE_FPU_CONTROL_WORD;
      TRICKHLA_VALIDATE_FPU_CONTROL_WORD;

      ostringstream errmsg;
      errmsg << "Federate::join_federation():" << __LINE__
             << " The Federate '" << get_federate_name()
             << "' is already a member of the '"
             << get_federation_name() << "' Federation." << THLA_ENDL;

      send_hs( stderr, (char *)errmsg.str().c_str() );
      exec_terminate( __FILE__, (char *)errmsg.str().c_str() );
   } catch ( RTI1516_NAMESPACE::FederationExecutionDoesNotExist &e ) {
      // The federation we created must have been destroyed by another
      // federate before we could join, so try again.
      this->federation_created_by_federate = false;
      this->federation_exists              = false;
      send_hs( stderr, "Federate::join_federation():%d EXCEPTION: %s Federation Execution does not exist.%c",
               __LINE__, get_federation_name(), THLA_NEWLINE );
   } catch ( RTI1516_NAMESPACE::SaveInProgress &e ) {
      send_hs( stderr, "Federate::join_federation():%d EXCEPTION: SaveInProgress%c", __LINE__, THLA_NEWLINE );
   } catch ( RTI1516_NAMESPACE::RestoreInProgress &e ) {
      send_hs( stderr, "Federate::join_federation():%d EXCEPTION: RestoreInProgress%c", __LINE__, THLA_NEWLINE );
   } catch ( RTI1516_NAMESPACE::NotConnected &e ) {
      // Macro to restore the saved FPU Control Word register value.
      TRICKHLA_RESTORE_FPU_CONTROL_WORD;
      TRICKHLA_VALIDATE_FPU_CONTROL_WORD;

      ostringstream errmsg;
      errmsg << "Federate::join_federation():" << __LINE__
             << " EXCEPTION: NotConnected" << THLA_ENDL;

      send_hs( stderr, (char *)errmsg.str().c_str() );
      exec_terminate( __FILE__, (char *)errmsg.str().c_str() );
   } catch ( RTI1516_NAMESPACE::CallNotAllowedFromWithinCallback &e ) {
      // Macro to restore the saved FPU Control Word register value.
      TRICKHLA_RESTORE_FPU_CONTROL_WORD;
      TRICKHLA_VALIDATE_FPU_CONTROL_WORD;

      ostringstream errmsg;
      errmsg << "Federate::join_federation():" << __LINE__
             << " EXCEPTION: CallNotAllowedFromWithinCallback" << THLA_ENDL;

      send_hs( stderr, (char *)errmsg.str().c_str() );
      exec_terminate( __FILE__, (char *)errmsg.str().c_str() );
   } catch ( RTI1516_NAMESPACE::RTIinternalError &e ) {
      // Macro to restore the saved FPU Control Word register value.
      TRICKHLA_RESTORE_FPU_CONTROL_WORD;
      TRICKHLA_VALIDATE_FPU_CONTROL_WORD;

      string rti_err_msg;
      StringUtilities::to_string( rti_err_msg, e.what() );
      ostringstream errmsg;
      errmsg << "Federate::join_federation():" << __LINE__
             << " Federate '" << get_federate_name() << "' for Federation '"
             << get_federation_name() << "' encountered RTI Internal Error: "
             << rti_err_msg << THLA_ENDL;

      send_hs( stderr, (char *)errmsg.str().c_str() );
      exec_terminate( __FILE__, (char *)errmsg.str().c_str() );
   }

   // Macro to restore the saved FPU Control Word register value.
   TRICKHLA_RESTORE_FPU_CONTROL_WORD;
   TRICKHLA_VALIDATE_FPU_CONTROL_WORD;
}

/*!
 * @job_class{initialization}
 */
void Federate::create_and_join_federation()
{
   if ( this->federation_joined ) {
      if ( should_print( DEBUG_LEVEL_2_TRACE, DEBUG_SOURCE_FEDERATE ) ) {
         ostringstream errmsg;
         errmsg << "Federate::create_and_join_federation():" << __LINE__
                << " Federation \"" << get_federation_name()
                << "\": ALREADY JOINED FEDERATION EXECUTION" << THLA_ENDL;
         send_hs( stderr, (char *)errmsg.str().c_str() );
      }
      return;
   }

   // Here we loop around the create and join federation calls until until we
   // are successful or hit the maximum number of attempts.
   const int max_retries = 100;

   for ( int k = 1; ( !this->federation_joined ) && ( k <= max_retries ); ++k ) {

      if ( !this->federation_exists ) {
         create_federation();
      }

      join_federation( get_federate_name(), get_federate_type() );

      if ( !this->federation_joined ) {
         send_hs( stderr, "Federate::create_and_join_federation():%d Failed to join federation \"%s\" on attempt %d of %d!%c",
                  __LINE__, get_federation_name(), k, max_retries,
                  THLA_NEWLINE );
         usleep( 100000 );
      }
   }

   if ( !this->federation_joined ) {
      ostringstream errmsg;
      errmsg << "Federate::create_and_join_federation():" << __LINE__
             << " Federate '" << get_federate_name() << "' FAILED TO JOIN the '"
             << get_federation_name() << "' Federation." << THLA_ENDL;

      send_hs( stderr, (char *)errmsg.str().c_str() );
      exec_terminate( __FILE__, (char *)errmsg.str().c_str() );
   }
}

/*!
 * @job_class{initialization}
 */
void Federate::enable_async_delivery()
{
   // Macro to save the FPU Control Word register value.
   TRICKHLA_SAVE_FPU_CONTROL_WORD;

   // Sanity check.
   if ( RTI_ambassador.get() == NULL ) {
      exec_terminate( __FILE__,
                      "Federate::enable_async_delivery() ERROR: NULL pointer to RTIambassador!" );
   }

   try {
      if ( should_print( DEBUG_LEVEL_2_TRACE, DEBUG_SOURCE_FEDERATE ) ) {
         send_hs( stdout, "Federate::enable_async_delivery():%d Enabling Asynchronous Delivery %c",
                  __LINE__, THLA_NEWLINE );
      }

      // Turn on asynchronous delivery of receive ordered messages. This will
      // allow us to receive messages that are not TimeStamp Ordered outside of
      // a time advancement.
      RTI_ambassador->enableAsynchronousDelivery();
   } catch ( AsynchronousDeliveryAlreadyEnabled &e ) {
      // Macro to restore the saved FPU Control Word register value.
      TRICKHLA_RESTORE_FPU_CONTROL_WORD;
      TRICKHLA_VALIDATE_FPU_CONTROL_WORD;
      send_hs( stderr, "Federate::enable_async_delivery():%d EXCEPTION: AsynchronousDeliveryAlreadyEnabled%c",
               __LINE__, THLA_NEWLINE );
   } catch ( SaveInProgress &e ) {
      // Macro to restore the saved FPU Control Word register value.
      TRICKHLA_RESTORE_FPU_CONTROL_WORD;
      TRICKHLA_VALIDATE_FPU_CONTROL_WORD;

      ostringstream errmsg;
      errmsg << "Federate::enable_async_delivery():" << __LINE__
             << " EXCEPTION: SaveInProgress" << THLA_ENDL;

      send_hs( stderr, (char *)errmsg.str().c_str() );
      exec_terminate( __FILE__, (char *)errmsg.str().c_str() );
   } catch ( RestoreInProgress &e ) {
      // Macro to restore the saved FPU Control Word register value.
      TRICKHLA_RESTORE_FPU_CONTROL_WORD;
      TRICKHLA_VALIDATE_FPU_CONTROL_WORD;

      ostringstream errmsg;
      errmsg << "Federate::enable_async_delivery():" << __LINE__
             << " EXCEPTION: RestoreInProgress" << THLA_ENDL;

      send_hs( stderr, (char *)errmsg.str().c_str() );
      exec_terminate( __FILE__, (char *)errmsg.str().c_str() );
   } catch ( FederateNotExecutionMember &e ) {
      // Macro to restore the saved FPU Control Word register value.
      TRICKHLA_RESTORE_FPU_CONTROL_WORD;
      TRICKHLA_VALIDATE_FPU_CONTROL_WORD;

      ostringstream errmsg;
      errmsg << "Federate::enable_async_delivery():" << __LINE__
             << " EXCEPTION: FederateNotExecutionMember" << THLA_ENDL;

      send_hs( stderr, (char *)errmsg.str().c_str() );
      exec_terminate( __FILE__, (char *)errmsg.str().c_str() );
   } catch ( NotConnected &e ) {
      // Macro to restore the saved FPU Control Word register value.
      TRICKHLA_RESTORE_FPU_CONTROL_WORD;
      TRICKHLA_VALIDATE_FPU_CONTROL_WORD;

      ostringstream errmsg;
      errmsg << "Federate::enable_async_delivery():" << __LINE__
             << " EXCEPTION: NotConnected" << THLA_ENDL;

      send_hs( stderr, (char *)errmsg.str().c_str() );
      exec_terminate( __FILE__, (char *)errmsg.str().c_str() );
   } catch ( RTIinternalError &e ) {
      // Macro to restore the saved FPU Control Word register value.
      TRICKHLA_RESTORE_FPU_CONTROL_WORD;
      TRICKHLA_VALIDATE_FPU_CONTROL_WORD;

      string rti_err_msg;
      StringUtilities::to_string( rti_err_msg, e.what() );

      ostringstream errmsg;
      errmsg << "Federate::enable_async_delivery():" << __LINE__
             << " EXCEPTION: RTIinternalError: '" << rti_err_msg << "'"
             << THLA_ENDL;

      send_hs( stderr, (char *)errmsg.str().c_str() );
      exec_terminate( __FILE__, (char *)errmsg.str().c_str() );
   } catch ( RTI1516_EXCEPTION &e ) {
      // Macro to restore the saved FPU Control Word register value.
      TRICKHLA_RESTORE_FPU_CONTROL_WORD;
      TRICKHLA_VALIDATE_FPU_CONTROL_WORD;

      string rti_err_msg;
      StringUtilities::to_string( rti_err_msg, e.what() );
      send_hs( stderr, "Federate::enable_async_delivery():%d \"%s\": Unexpected RTI exception!\nRTI Exception: RTIinternalError: '%s'\n%c",
               __LINE__, get_federation_name(), rti_err_msg.c_str(), THLA_NEWLINE );
   }
   // Macro to restore the saved FPU Control Word register value.
   TRICKHLA_RESTORE_FPU_CONTROL_WORD;
   TRICKHLA_VALIDATE_FPU_CONTROL_WORD;
}

/*!
 * @job_class{shutdown}
 */
bool Federate::check_for_shutdown()
{
   return ( execution_control->check_for_shutdown() );
}

/*!
 * @details NOTE: If a shutdown has been announced, this routine calls the
 * Trick exec_teminate() function.  So, for shutdown, it should never return.
 * @job_class{shutdown}
 */
bool Federate::check_for_shutdown_with_termination()
{
   return ( execution_control->check_for_shutdown_with_termination() );
}

/*!
* @job_class{initialization}.
*/
void Federate::setup_time_management()
{
   // Disable time management if the federate is not setup to be
   // time-regulating or time-constrained.
   if ( time_management && !time_regulating && !time_constrained ) {
      time_management = false;
   }

   if ( should_print( DEBUG_LEVEL_2_TRACE, DEBUG_SOURCE_FEDERATE ) ) {
      send_hs( stdout, "Federate::setup_time_management():%d time_management:%s time_constrained:%s time_regulating:%s %c",
               __LINE__,
               ( time_management ? "Yes" : "No" ),
               ( time_constrained ? "Yes" : "No" ),
               ( time_regulating ? "Yes" : "No" ), THLA_NEWLINE );
   }

   // Determine if HLA time management is enabled.
   if ( time_management ) {

      // Setup time constrained if the user wants to be constrained and our
      // current HLA time constrained state indicates we are not constrained.
      if ( time_constrained && !time_constrained_state ) {
         setup_time_constrained();
      } else if ( !time_constrained && time_constrained_state ) {
         // Disable time constrained if our current HLA state indicates we
         // are already constrained.
         shutdown_time_constrained();
      }

      // Setup time regulation if the user wanted to be regulated and our
      // current HLA time regulating state indicates we are not regulated.
      if ( time_regulating && !time_regulating_state ) {
         setup_time_regulation();
      } else if ( !time_regulating && time_regulating_state ) {
         // Disable time regulation if our current HLA state indicates we
         // are already regulating.
         shutdown_time_regulating();
      }
   } else {
      // HLA Time Management is disabled.

      // Disable time constrained and time regulation.
      if ( time_constrained_state ) {
         shutdown_time_constrained();
      }
      if ( time_regulating_state ) {
         shutdown_time_regulating();
      }
   }
}

/*!
* @job_class{initialization}.
*/
void Federate::setup_time_constrained()
{
   // Just return if HLA time management is not enabled, the user does
   // not want time constrained enabled, or if we are already constrained.
   if ( !time_management || !time_constrained || time_constrained_state ) {
      return;
   }

   // Macro to save the FPU Control Word register value.
   TRICKHLA_SAVE_FPU_CONTROL_WORD;

   // Sanity check.
   if ( RTI_ambassador.get() == NULL ) {
      exec_terminate( __FILE__,
                      "Federate::setup_time_constrained() ERROR: NULL pointer to RTIambassador!" );
   }

   try {
      if ( should_print( DEBUG_LEVEL_2_TRACE, DEBUG_SOURCE_FEDERATE ) ) {
         send_hs( stdout, "Federate::setup_time_constrained()%d \"%s\": ENABLING TIME CONSTRAINED %c",
                  __LINE__, get_federation_name(), THLA_NEWLINE );
      }

      time_adv_grant         = false;
      time_constrained_state = false;

      // Turn on constrained status so that regulating federates will control
      // our advancement in time.
      //
      // If we are constrained and sending federates specify the Class
      // attributes and Communication interaction with timestamp in the
      // simulation fed file we will receive TimeStamp Ordered messages.
      RTI_ambassador->enableTimeConstrained();

      unsigned int sleep_micros = 1000;
      unsigned int wait_count   = 0;
      unsigned int wait_check   = 10000000 / sleep_micros; // Number of wait cycles for 10 seconds

      // This spin lock waits for the time constrained flag to be set from the RTI.
      while ( !time_constrained_state ) {

         // Check for shutdown.
         this->check_for_shutdown_with_termination();

         usleep( sleep_micros );

         if ( ( !time_constrained_state ) && ( ( ++wait_count % wait_check ) == 0 ) ) {
            wait_count = 0;
            if ( !is_execution_member() ) {
               ostringstream errmsg;
               errmsg << "Federate::setup_time_constrained():" << __LINE__
                      << " Unexpectedly the Federate is no longer an execution"
                      << " member. This means we are either not connected to the"
                      << " RTI or we are no longer joined to the federation"
                      << " execution because someone forced our resignation at"
                      << " the Central RTI Component (CRC) level!"
                      << THLA_ENDL;
               send_hs( stderr, (char *)errmsg.str().c_str() );
               exec_terminate( __FILE__, (char *)errmsg.str().c_str() );
            }
         }
      }
   } catch ( RTI1516_NAMESPACE::TimeConstrainedAlreadyEnabled &e ) {
      // Macro to restore the saved FPU Control Word register value.
      TRICKHLA_RESTORE_FPU_CONTROL_WORD;
      TRICKHLA_VALIDATE_FPU_CONTROL_WORD;

      time_constrained_state = true;

      string rti_err_msg;
      StringUtilities::to_string( rti_err_msg, e.what() );
      send_hs( stderr, "Federate::setup_time_constrained():%d \"%s\": Time Constrained Already Enabled : '%s'%c",
               __LINE__, get_federation_name(), rti_err_msg.c_str(), THLA_NEWLINE );
   } catch ( RTI1516_NAMESPACE::InTimeAdvancingState &e ) {
      // Macro to restore the saved FPU Control Word register value.
      TRICKHLA_RESTORE_FPU_CONTROL_WORD;
      TRICKHLA_VALIDATE_FPU_CONTROL_WORD;

      string rti_err_msg;
      StringUtilities::to_string( rti_err_msg, e.what() );
      send_hs( stderr, "Federate::setup_time_constrained():%d \"%s\": ERROR: InTimeAdvancingState : '%s'%c",
               __LINE__, get_federation_name(), rti_err_msg.c_str(), THLA_NEWLINE );
   } catch ( RTI1516_NAMESPACE::RequestForTimeConstrainedPending &e ) {
      // Macro to restore the saved FPU Control Word register value.
      TRICKHLA_RESTORE_FPU_CONTROL_WORD;
      TRICKHLA_VALIDATE_FPU_CONTROL_WORD;

      string rti_err_msg;
      StringUtilities::to_string( rti_err_msg, e.what() );
      send_hs( stderr, "Federate::setup_time_constrained():%d \"%s\": ERROR: RequestForTimeConstrainedPending : '%s'%c",
               __LINE__, get_federation_name(), rti_err_msg.c_str(), THLA_NEWLINE );
   } catch ( RTI1516_NAMESPACE::FederateNotExecutionMember &e ) {
      // Macro to restore the saved FPU Control Word register value.
      TRICKHLA_RESTORE_FPU_CONTROL_WORD;
      TRICKHLA_VALIDATE_FPU_CONTROL_WORD;

      string rti_err_msg;
      StringUtilities::to_string( rti_err_msg, e.what() );
      send_hs( stderr, "Federate::setup_time_constrained():%d \"%s\": ERROR: FederateNotExecutionMember : '%s'%c",
               __LINE__, get_federation_name(), rti_err_msg.c_str(), THLA_NEWLINE );
   } catch ( RTI1516_NAMESPACE::SaveInProgress &e ) {
      // Macro to restore the saved FPU Control Word register value.
      TRICKHLA_RESTORE_FPU_CONTROL_WORD;
      TRICKHLA_VALIDATE_FPU_CONTROL_WORD;

      string rti_err_msg;
      StringUtilities::to_string( rti_err_msg, e.what() );
      send_hs( stderr, "TrickHLAFderate::setup_time_constrained():%d \"%s\": ERROR: SaveInProgress : '%s'%c",
               __LINE__, get_federation_name(), rti_err_msg.c_str(), THLA_NEWLINE );
   } catch ( RTI1516_NAMESPACE::RestoreInProgress &e ) {
      // Macro to restore the saved FPU Control Word register value.
      TRICKHLA_RESTORE_FPU_CONTROL_WORD;
      TRICKHLA_VALIDATE_FPU_CONTROL_WORD;

      string rti_err_msg;
      StringUtilities::to_string( rti_err_msg, e.what() );
      send_hs( stderr, "Federate::setup_time_constrained():%d \"%s\": ERROR: RestoreInProgress : '%s'%c",
               __LINE__, get_federation_name(), rti_err_msg.c_str(), THLA_NEWLINE );
   } catch ( RTI1516_NAMESPACE::NotConnected &e ) {
      // Macro to restore the saved FPU Control Word register value.
      TRICKHLA_RESTORE_FPU_CONTROL_WORD;
      TRICKHLA_VALIDATE_FPU_CONTROL_WORD;

      string rti_err_msg;
      StringUtilities::to_string( rti_err_msg, e.what() );
      send_hs( stderr, "Federate::setup_time_constrained():%d \"%s\": ERROR: NotConnected : '%s'%c",
               __LINE__, get_federation_name(), rti_err_msg.c_str(), THLA_NEWLINE );
   } catch ( RTI1516_NAMESPACE::RTIinternalError &e ) {
      // Macro to restore the saved FPU Control Word register value.
      TRICKHLA_RESTORE_FPU_CONTROL_WORD;
      TRICKHLA_VALIDATE_FPU_CONTROL_WORD;

      string rti_err_msg;
      StringUtilities::to_string( rti_err_msg, e.what() );
      send_hs( stderr, "Federate::setup_time_constrained():%d \"%s\": ERROR: RTIinternalError : '%s'%c",
               __LINE__, get_federation_name(), rti_err_msg.c_str(), THLA_NEWLINE );
   } catch ( RTI1516_EXCEPTION &e ) {
      // Macro to restore the saved FPU Control Word register value.
      TRICKHLA_RESTORE_FPU_CONTROL_WORD;
      TRICKHLA_VALIDATE_FPU_CONTROL_WORD;

      string rti_err_msg;
      StringUtilities::to_string( rti_err_msg, e.what() );
      send_hs( stderr, "Federate::setup_time_constrained():%d \"%s\": Unexpected RTI exception!\nRTI Exception: RTIinternalError: '%s'%c",
               __LINE__, get_federation_name(), rti_err_msg.c_str(), THLA_NEWLINE );
   }
   // Macro to restore the saved FPU Control Word register value.
   TRICKHLA_RESTORE_FPU_CONTROL_WORD;
   TRICKHLA_VALIDATE_FPU_CONTROL_WORD;
}

/*!
* @job_class{initialization}.
*/
void Federate::setup_time_regulation()
{
   // Just return if HLA time management is not enabled, the user does
   // not want time regulation enabled, or if we are already regulating.
   if ( !time_management || !time_regulating || time_regulating_state ) {
      return;
   }

   // Macro to save the FPU Control Word register value.
   TRICKHLA_SAVE_FPU_CONTROL_WORD;

   // Sanity check.
   if ( RTI_ambassador.get() == NULL ) {
      exec_terminate( __FILE__,
                      "Federate::setup_time_regulation() ERROR: NULL pointer to RTIambassador!" );
   }

   try {
      if ( should_print( DEBUG_LEVEL_2_TRACE, DEBUG_SOURCE_FEDERATE ) ) {
         send_hs( stdout, "Federate::setup_time_regulation():%d \"%s\": ENABLING TIME REGULATION WITH LOOKAHEAD = %G seconds.%c",
                  __LINE__, get_federation_name(), lookahead.getDoubleTime(), THLA_NEWLINE );
      }

      // RTI_amb->enableTimeRegulation() is an implicit
      // RTI_amb->timeAdvanceRequest() so clear the flags since we will get a
      // FedAmb::timeRegulationEnabled() callback which will set the
      // time_adv_grant and time_regulating_state flags to true.

      time_adv_grant        = false;
      time_regulating_state = false;

      // Turn on regulating status so that constrained federates will be
      // controlled by our time.
      //
      // If we are regulating and our object attributes and interaction
      // parameters are specified with timestamp in the FOM we will send
      // TimeStamp Ordered messages.
      RTI_ambassador->enableTimeRegulation( lookahead.get() );

      unsigned int sleep_micros = 1000;
      unsigned int wait_count   = 0;
      unsigned int wait_check   = 10000000 / sleep_micros; // Number of wait cycles for 10 seconds

      // This spin lock waits for the time regulation flag to be set from the RTI.
      while ( !time_regulating_state ) {

         // Check for shutdown.
         this->check_for_shutdown_with_termination();

         usleep( sleep_micros );

         if ( ( !time_regulating_state ) && ( ( ++wait_count % wait_check ) == 0 ) ) {
            wait_count = 0;
            if ( !is_execution_member() ) {
               ostringstream errmsg;
               errmsg << "Federate::setup_time_regulation():" << __LINE__
                      << " Unexpectedly the Federate is no longer an execution"
                      << " member. This means we are either not connected to the"
                      << " RTI or we are no longer joined to the federation"
                      << " execution because someone forced our resignation at"
                      << " the Central RTI Component (CRC) level!"
                      << THLA_ENDL;
               send_hs( stderr, (char *)errmsg.str().c_str() );
               exec_terminate( __FILE__, (char *)errmsg.str().c_str() );
            }
         }
      }

   } catch ( RTI1516_NAMESPACE::TimeRegulationAlreadyEnabled &e ) {
      // Macro to restore the saved FPU Control Word register value.
      TRICKHLA_RESTORE_FPU_CONTROL_WORD;
      TRICKHLA_VALIDATE_FPU_CONTROL_WORD;

      time_regulating_state = true;

      string rti_err_msg;
      StringUtilities::to_string( rti_err_msg, e.what() );
      send_hs( stderr, "Federate::setup_time_regulation():%d \"%s\": Time Regulation Already Enabled: '%s'%c",
               __LINE__, get_federation_name(), rti_err_msg.c_str(), THLA_NEWLINE );
   } catch ( RTI1516_NAMESPACE::InvalidLookahead &e ) {
      // Macro to restore the saved FPU Control Word register value.
      TRICKHLA_RESTORE_FPU_CONTROL_WORD;
      TRICKHLA_VALIDATE_FPU_CONTROL_WORD;

      string rti_err_msg;
      StringUtilities::to_string( rti_err_msg, e.what() );
      send_hs( stderr, "Federate::setup_time_regulation():%d \"%s\": ERROR: InvalidLookahead: '%s'%c",
               __LINE__, get_federation_name(), rti_err_msg.c_str(), THLA_NEWLINE );
   } catch ( RTI1516_NAMESPACE::InTimeAdvancingState &e ) {
      // Macro to restore the saved FPU Control Word register value.
      TRICKHLA_RESTORE_FPU_CONTROL_WORD;
      TRICKHLA_VALIDATE_FPU_CONTROL_WORD;

      string rti_err_msg;
      StringUtilities::to_string( rti_err_msg, e.what() );
      send_hs( stderr, "Federate::setup_time_regulation():%d \"%s\": ERROR: InTimeAdvancingState: '%s'%c",
               __LINE__, get_federation_name(), rti_err_msg.c_str(), THLA_NEWLINE );
   } catch ( RTI1516_NAMESPACE::RequestForTimeRegulationPending &e ) {
      // Macro to restore the saved FPU Control Word register value.
      TRICKHLA_RESTORE_FPU_CONTROL_WORD;
      TRICKHLA_VALIDATE_FPU_CONTROL_WORD;

      string rti_err_msg;
      StringUtilities::to_string( rti_err_msg, e.what() );
      send_hs( stderr, "Federate::setup_time_regulation():%d \"%s\": ERROR: RequestForTimeRegulationPending: '%s'%c",
               __LINE__, get_federation_name(), rti_err_msg.c_str(), THLA_NEWLINE );
   } catch ( RTI1516_NAMESPACE::FederateNotExecutionMember &e ) {
      // Macro to restore the saved FPU Control Word register value.
      TRICKHLA_RESTORE_FPU_CONTROL_WORD;
      TRICKHLA_VALIDATE_FPU_CONTROL_WORD;

      string rti_err_msg;
      StringUtilities::to_string( rti_err_msg, e.what() );
      send_hs( stderr, "Federate::setup_time_regulation():%d \"%s\": ERROR: FederateNotExecutionMember: '%s'%c",
               __LINE__, get_federation_name(), rti_err_msg.c_str(), THLA_NEWLINE );
   } catch ( RTI1516_NAMESPACE::SaveInProgress &e ) {
      // Macro to restore the saved FPU Control Word register value.
      TRICKHLA_RESTORE_FPU_CONTROL_WORD;
      TRICKHLA_VALIDATE_FPU_CONTROL_WORD;

      string rti_err_msg;
      StringUtilities::to_string( rti_err_msg, e.what() );
      send_hs( stderr, "Federate::setup_time_regulation():%d \"%s\": ERROR: SaveInProgress: '%s'%c",
               __LINE__, get_federation_name(), rti_err_msg.c_str(), THLA_NEWLINE );
   } catch ( RTI1516_NAMESPACE::RestoreInProgress &e ) {
      // Macro to restore the saved FPU Control Word register value.
      TRICKHLA_RESTORE_FPU_CONTROL_WORD;
      TRICKHLA_VALIDATE_FPU_CONTROL_WORD;

      string rti_err_msg;
      StringUtilities::to_string( rti_err_msg, e.what() );
      send_hs( stderr, "Federate::setup_time_regulation():%d \"%s\": ERROR: RestoreInProgress: '%s'%c",
               __LINE__, get_federation_name(), rti_err_msg.c_str(), THLA_NEWLINE );
   } catch ( RTI1516_NAMESPACE::NotConnected &e ) {
      // Macro to restore the saved FPU Control Word register value.
      TRICKHLA_RESTORE_FPU_CONTROL_WORD;
      TRICKHLA_VALIDATE_FPU_CONTROL_WORD;

      string rti_err_msg;
      StringUtilities::to_string( rti_err_msg, e.what() );
      send_hs( stderr, "Federate::setup_time_regulation():%d \"%s\": ERROR: NotConnected : '%s'%c",
               __LINE__, get_federation_name(), rti_err_msg.c_str(), THLA_NEWLINE );
   } catch ( RTI1516_NAMESPACE::RTIinternalError &e ) {
      // Macro to restore the saved FPU Control Word register value.
      TRICKHLA_RESTORE_FPU_CONTROL_WORD;
      TRICKHLA_VALIDATE_FPU_CONTROL_WORD;

      string rti_err_msg;
      StringUtilities::to_string( rti_err_msg, e.what() );
      send_hs( stderr, "Federate::setup_time_regulation():%d \"%s\": ERROR: RTIinternalError: '%s'%c",
               __LINE__, get_federation_name(), rti_err_msg.c_str(), THLA_NEWLINE );
   } catch ( RTI1516_EXCEPTION &e ) {
      // Macro to restore the saved FPU Control Word register value.
      TRICKHLA_RESTORE_FPU_CONTROL_WORD;
      TRICKHLA_VALIDATE_FPU_CONTROL_WORD;

      string rti_err_msg;
      StringUtilities::to_string( rti_err_msg, e.what() );
      send_hs( stderr, "Federate::setup_time_regulation():%d \"%s\": Unexpected RTI exception!\nRTI Exception: RTIinternalError: '%s'%c",
               __LINE__, get_federation_name(), rti_err_msg.c_str(), THLA_NEWLINE );
   }
   // Macro to restore the saved FPU Control Word register value.
   TRICKHLA_RESTORE_FPU_CONTROL_WORD;
   TRICKHLA_VALIDATE_FPU_CONTROL_WORD;
}

/*!
 * @job_class{scheduled}
 */
void Federate::time_advance_request()
{
   // Skip requesting time-advancement if we are not time-regulating and
   // not time-constrained (i.e. not using time management).
   if ( !time_management ) {
      return;
   }

   // Do not ask for a time advance on an initialization pass.
   if ( exec_get_mode() == Initialization ) {
      if ( should_print( DEBUG_LEVEL_2_TRACE, DEBUG_SOURCE_FEDERATE ) ) {
         send_hs( stdout, "Federate::time_advance_request():%d exec_init_pass() == true so returning.%c",
                  __LINE__, THLA_NEWLINE );
      }
      return;
   }

   // -- start of checkpoint additions --
   save_completed = false; // reset ONLY at the bottom of the frame...
   // -- end of checkpoint additions --

   // Build a request time.
   requested_time += lookahead_time; //TODO: Use job cycle time instead.

   // Perform the time-advance request to go to the requested time.
   perform_time_advance_request();
}

/*!
 * @job_class{scheduled}
 */
void Federate::perform_time_advance_request()
{
   // Skip requesting time-advancement if we are not time-regulating and
   // not time-constrained (i.e. not using time management).
   if ( !time_management ) {
      return;
   }

   // -- start of checkpoint additions --
   save_completed = false; // reset ONLY at the bottom of the frame...
   // -- end of checkpoint additions --

   bool anyError, isRecoverableError;
   int  errorRecoveryCnt   = 0;
   int  max_retry_attempts = 1000;

   // Macro to save the FPU Control Word register value.
   TRICKHLA_SAVE_FPU_CONTROL_WORD;

   do {
      // Reset the error flags.
      anyError           = false;
      isRecoverableError = false;

      // Check for shutdown.
      this->check_for_shutdown_with_termination();

      if ( should_print( DEBUG_LEVEL_4_TRACE, DEBUG_SOURCE_FEDERATE ) ) {
         send_hs( stdout, "Federate::perform_time_advance_request():%d Time Advance Request (TAR) to %.12G seconds.%c",
                  __LINE__, requested_time.getDoubleTime(), THLA_NEWLINE );
      }

      try {
         // Mark that the time advance has not yet been granted. This variable
         // will be updated in the FederateAmbassador class callback.
         time_adv_grant = false;

         // Request that time be advanced to the new time.
         RTI_ambassador->timeAdvanceRequest( requested_time.get() );
      } catch ( InvalidLogicalTime &e ) {
         anyError           = true;
         isRecoverableError = false;
         send_hs( stderr, "Federate::perform_time_advance_request():%d EXCEPTION: InvalidLogicalTime%c",
                  __LINE__, THLA_NEWLINE );
      } catch ( LogicalTimeAlreadyPassed &e ) {
         anyError           = false;
         isRecoverableError = false;
         send_hs( stderr, "Federate::perform_time_advance_request():%d EXCEPTION: LogicalTimeAlreadyPassed%c",
                  __LINE__, THLA_NEWLINE );
      } catch ( InTimeAdvancingState &e ) {
         // A time advance request is still being processed by the RTI so print
         // a message and treat this as a successful time advance request.
         send_hs( stderr, "Federate::perform_time_advance_request():%d WARNING: Ignoring InTimeAdvancingState HLA Exception.%c",
                  __LINE__, THLA_NEWLINE );
      } catch ( RequestForTimeRegulationPending &e ) {
         anyError           = true;
         isRecoverableError = true;
         send_hs( stderr, "Federate::perform_time_advance_request():%d EXCEPTION: RequestForTimeRegulationPending%c",
                  __LINE__, THLA_NEWLINE );
      } catch ( RequestForTimeConstrainedPending &e ) {
         anyError           = true;
         isRecoverableError = true;
         send_hs( stderr, "Federate::perform_time_advance_request():%d EXCEPTION: RequestForTimeConstrainedPending%c",
                  __LINE__, THLA_NEWLINE );
      } catch ( FederateNotExecutionMember &e ) {
         anyError           = true;
         isRecoverableError = false;
         send_hs( stderr, "Federate::perform_time_advance_request():%d EXCEPTION: FederateNotExecutionMember%c",
                  __LINE__, THLA_NEWLINE );
      } catch ( SaveInProgress &e ) {
         anyError           = true;
         isRecoverableError = true;
         send_hs( stderr, "Federate::perform_time_advance_request():%d EXCEPTION: SaveInProgress%c",
                  __LINE__, THLA_NEWLINE );
      } catch ( RestoreInProgress &e ) {
         anyError           = true;
         isRecoverableError = true;
         send_hs( stderr, "Federate::perform_time_advance_request():%d EXCEPTION: RestoreInProgress%c",
                  __LINE__, THLA_NEWLINE );
      } catch ( NotConnected &e ) {
         anyError           = true;
         isRecoverableError = false;
         send_hs( stderr, "Federate::perform_time_advance_request():%d EXCEPTION: NotConnected%c",
                  __LINE__, THLA_NEWLINE );
      } catch ( RTIinternalError &e ) {
         anyError           = true;
         isRecoverableError = false;

         string rti_err_msg;
         StringUtilities::to_string( rti_err_msg, e.what() );
         send_hs( stderr, "Federate::perform_time_advance_request():%d \"%s\": Unexpected RTI exception!\n RTI Exception: RTIinternalError: '%s'%c",
                  __LINE__, get_federation_name(), rti_err_msg.c_str(), THLA_NEWLINE );
      }

      // For any recoverable error, count the error and wait for a little while
      // before trying again.
      if ( anyError && isRecoverableError ) {
         errorRecoveryCnt++;
         send_hs( stderr, "Federate::perform_time_advance_request():%d Recoverable RTI error, retry attempt: %d%c",
                  __LINE__, errorRecoveryCnt, THLA_NEWLINE );

         usleep( 1000 );
      }

   } while ( anyError && isRecoverableError && ( errorRecoveryCnt < max_retry_attempts ) );

   // Macro to restore the saved FPU Control Word register value.
   TRICKHLA_RESTORE_FPU_CONTROL_WORD;
   TRICKHLA_VALIDATE_FPU_CONTROL_WORD;

   // If we have any errors at this point or exceed the maximum error
   // recovery attempts then display an error message and exit.
   if ( anyError ) {
      send_hs( stderr, "Federate::perform_time_advance_request():%d \"%s\": Unrecoverable RTI Error, exiting!%c",
               __LINE__, get_federation_name(), THLA_NEWLINE );
      exec_terminate( __FILE__, "Federate::perform_time_advance_request() ERROR: Unrecoverable RTI Error, exiting!!" );
      exit( 1 );
   }
}

/*!
 *  @job_class{scheduled}
 */
void Federate::wait_for_time_advance_grant()
{
   // Skip requesting time-advancement if we are not time-regulating and
   // not time-constrained (i.e. not using time management).
   if ( !time_management ) {
      return;
   }

   // Do not ask for a time advance on an initialization pass.
   if ( exec_get_mode() == Initialization ) {
      if ( should_print( DEBUG_LEVEL_1_TRACE, DEBUG_SOURCE_FEDERATE ) ) {
         send_hs( stdout, "Federate::wait_for_time_advance_grant():%d In Initialization mode so returning.%c",
                  __LINE__, THLA_NEWLINE );
      }
      return;
   }

   if ( should_print( DEBUG_LEVEL_5_TRACE, DEBUG_SOURCE_FEDERATE ) ) {
      send_hs( stdout, "Federate::wait_for_time_advance_grant():%d Waiting for Time Advance Grant (TAG) to %.12G seconds.%c",
               __LINE__, requested_time.getDoubleTime(), THLA_NEWLINE );
   }

   if ( !time_adv_grant ) {

      // NOTE: The RELEASE_1() call is almost 5 times faster than the usleep()
      // call. However, the speed is system specific so we can not reliably
      // determine the number of wait-check loops equals 10 seconds, so we use
      // usleep(). Because we can reliably determine the wait-check it could
      // result in the check for HLA execution member calling the RTI very
      // frequently resulting in an RTI performance problem. If we are using
      // Central Timing Equipment (CTE) then we may want to revert back to
      // RELEASE_1() so that our polling is much faster as usleep() is
      // 1 millisecond (minimum kernel time).  Dan Dexter, NASA/ER7, June 2016
#define THLA_TAG_USE_USLEEP 1
#if THLA_TAG_USE_USLEEP
      unsigned int sleep_micros = 1000;
      unsigned int wait_count   = 0;
      unsigned int wait_check   = 10000000 / sleep_micros; // Number of wait cycles for 10 seconds using usleep()
#else
      unsigned int wait_count = 0;
      unsigned int wait_check = 50000000; // Number of wait cycles for 10 seconds using RELEASE_1()
#endif

      // This spin lock waits for the time advance grant from the RTI.
      while ( !time_adv_grant ) {

         // Check for shutdown.
         this->check_for_shutdown_with_termination();

#if THLA_TAG_USE_USLEEP
         usleep( sleep_micros );
#else
         RELEASE_1();                     // Faster than usleep()
#endif
         if ( ( !time_adv_grant ) && ( ( ++wait_count % wait_check ) == 0 ) ) {
            wait_count = 0;
            if ( is_execution_member() ) {
               if ( should_print( DEBUG_LEVEL_4_TRACE, DEBUG_SOURCE_FEDERATE ) ) {
                  send_hs( stdout, "Federate::wait_for_time_advance_grant():%d Still Execution Member.%c",
                           __LINE__, THLA_NEWLINE );
               }
            } else {
               ostringstream errmsg;
               errmsg << "Federate::wait_for_time_advance_grant():" << __LINE__
                      << " Unexpectedly the Federate is no longer an execution"
                      << " member. This means we are either not connected to the"
                      << " RTI or we are no longer joined to the federation"
                      << " execution because someone forced our resignation at"
                      << " the Central RTI Component (CRC) level!"
                      << THLA_ENDL;
               send_hs( stderr, (char *)errmsg.str().c_str() );
               exec_terminate( __FILE__, (char *)errmsg.str().c_str() );
            }
         }
      }
   }

   // Record the granted time in the HLA_time variable, so we can plot it in Trick.
   this->HLA_time = get_granted_time();

   // Add the line number for a higher trace level.
   if ( should_print( DEBUG_LEVEL_4_TRACE, DEBUG_SOURCE_FEDERATE ) ) {
      send_hs( stdout, "Federate::wait_for_time_advance_grant():%d Time Advance Grant (TAG) to %.12G seconds.%c",
               __LINE__, HLA_time, THLA_NEWLINE );
   }
}

/*!
 * \par<b>Assumptions and Limitations:</b>
 * - Currently only used with DIS initialization scheme.
 *  @job_class{scheduled}
 */
void Federate::wait_for_time_advance_grant(
   int time_out_tolerance )
{

   // Skip requesting time-advancement if we are not time-regulating and
   // not time-constrained (i.e. not using time management).
   if ( !time_management ) {
      return;
   }

   // Do not ask for a time advance on an initialization pass.
   if ( exec_get_mode() == Initialization ) {
      if ( should_print( DEBUG_LEVEL_1_TRACE, DEBUG_SOURCE_FEDERATE ) ) {
         send_hs( stdout, "Federate::wait_for_time_advance_grant():%d N/A because in Initialization mode.%c",
                  __LINE__, THLA_NEWLINE );
      }
      return;
   }

   if ( should_print( DEBUG_LEVEL_5_TRACE, DEBUG_SOURCE_FEDERATE ) ) {
      send_hs( stdout, "Federate::wait_for_time_advance_grant():%d Waiting for Time Advance Grant (TAG) to %.12G seconds.%c",
               __LINE__, requested_time.getDoubleTime(), THLA_NEWLINE );
   }

   // This spin lock waits for the time advance grant from the RTI.
   stale_data_counter = 0;
   int time_out       = 0;
   while ( ( !time_adv_grant ) && ( time_out <= time_out_tolerance ) ) {

      // Check for shutdown.
      this->check_for_shutdown_with_termination();

      time_out++;
      RELEASE_1();

      // Don't wait anymore if past time_out_tolerance
      if ( time_out > time_out_tolerance ) {
         stale_data_counter++;
      }
   }

   // Record the granted time in the HLA_time variable, so we can plot it in Trick.
   this->HLA_time = get_granted_time();

   // Add the line number for a higher trace level.
   if ( should_print( DEBUG_LEVEL_4_TRACE, DEBUG_SOURCE_FEDERATE ) ) {
      send_hs( stdout, "Federate::wait_for_time_advance_grant():%d Time Advance Grant (TAG) to %.12G seconds.%c",
               __LINE__, HLA_time, THLA_NEWLINE );
   }
}

/*!
 *  @job_class{scheduled}
 */
bool Federate::is_execution_member()
{
   if ( RTI_ambassador.get() != NULL ) {
      bool is_exec_member = true;
      try {
         RTI_ambassador->getOrderName( TIMESTAMP );
      } catch ( RTI1516_NAMESPACE::InvalidOrderType &e ) {
         // Do nothing
      } catch ( RTI1516_NAMESPACE::FederateNotExecutionMember &e ) {
         is_exec_member = false;
      } catch ( RTI1516_NAMESPACE::NotConnected &e ) {
         is_exec_member = false;
      } catch ( RTI1516_NAMESPACE::RTIinternalError &e ) {
         // Do nothing
      }
      return is_exec_member;
   }
   return false;
}

/*!
 *  @details Shutdown the federate by shutting down the time management,
 *  resigning from the federation, and then attempt to destroy the federation.
 *  @job_class{shutdown}
 */
void Federate::shutdown()
{
   // We can only shutdown if we have a name since shutdown could have been
   // called in the destructor, so we guard against that.
   if ( !shutdown_called ) {
      shutdown_called = true;

      if ( should_print( DEBUG_LEVEL_2_TRACE, DEBUG_SOURCE_FEDERATE ) ) {
         send_hs( stdout, "Federate::shutdown():%d %c", __LINE__,
                  THLA_NEWLINE );
      }

      // Check for Execution Control shutdown.  If this is NULL, then we are
      // probably shutting down prior to initialization.
      if ( execution_control != NULL ) {
         // Call Execution Control shutdown method.
         execution_control->shutdown();
      }

      // Shutdown the manager.
      if ( manager != NULL ) {
         manager->shutdown();
      }

      // Macro to save the FPU Control Word register value.
      TRICKHLA_SAVE_FPU_CONTROL_WORD;

      // Disable Time Constrained and Time Regulation for this federate.
      this->shutdown_time_management();

      // Resign from the federation.
      // If the federate can rejoin, resign in a way so we can rejoin later...
      if ( can_rejoin_federation ) {
         this->resign_so_we_can_rejoin();
      } else {
         this->resign();
      }

      // Attempt to destroy the federation.
      this->destroy();

      // Remove the ExecutionConfiguration object.
      if ( execution_control != NULL ) {
         execution_control->remove_execution_configuration();
      }

      // Macro to restore the saved FPU Control Word register value.
      TRICKHLA_RESTORE_FPU_CONTROL_WORD;

#if defined( FPU_CW_PROTECTION ) && ( defined( __i386__ ) || defined( __x86_64__ ) )
      // As the last thing we do, check to see if we did a good job of
      // protecting against FPU control-word precision-control changes by
      // comparing the current precision-control value to the one at program
      // startup (__fpu_control is automatically set for us, and the _fpu_cw
      // variable comes from the TRICKHLA_SAVE_FPU_CONTROL_WORD macro). Print
      // a warning message if they are different. Only support the Intel CPU's.
      // NOTE: Don't use the TRICKHLA_VALIDATE_FPU_CONTROL_WORD because it can
      // be disabled in the TrickHLA compile-config header file.
      if ( ( _fpu_cw & _FPU_PC_MASK ) != ( __fpu_control & _FPU_PC_MASK ) ) {
         send_hs( stderr, "%s:%d WARNING: We have detected that the current \
Floating-Point Unit (FPU) Control-Word Precision-Control value (%#x: %s) does \
not match the Precision-Control value at program startup (%#x: %s). The change \
in FPU Control-Word Precision-Control could cause the numerical values in your \
simulation to be slightly different in the 7th or 8th decimal place. Please \
contact the TrickHLA team for support.%c",
                  __FILE__, __LINE__,
                  ( _fpu_cw & _FPU_PC_MASK ), _FPU_PC_PRINT( _fpu_cw ),
                  ( __fpu_control & _FPU_PC_MASK ), _FPU_PC_PRINT( __fpu_control ),
                  THLA_NEWLINE );
      }
#endif
   }

   return;
}

/*!
 *  @details Shutdown this federate's time management by shutting down time
 *  constraint management and time regulating management.
 *  @job_class{shutdown}
 */
void Federate::shutdown_time_management()
{
   shutdown_time_constrained();
   shutdown_time_regulating();
}

/*!
 *  @job_class{shutdown}
 */
void Federate::shutdown_time_constrained()
{
   if ( !time_constrained_state ) {
      if ( should_print( DEBUG_LEVEL_2_TRACE, DEBUG_SOURCE_FEDERATE ) ) {
         send_hs( stdout, "Federate::shutdown_time_constrained():%d HLA Time Constrained Already Disabled.%c",
                  __LINE__, THLA_NEWLINE );
      }
   } else {
      // Macro to save the FPU Control Word register value.
      TRICKHLA_SAVE_FPU_CONTROL_WORD;

      // Make sure we've been able to get the RTI ambassador.
      if ( RTI_ambassador.get() == NULL ) {
         return;
      }

      if ( should_print( DEBUG_LEVEL_2_TRACE, DEBUG_SOURCE_FEDERATE ) ) {
         send_hs( stdout, "Federate::shutdown_time_constrained():%d Disabling HLA Time Constrained.%c",
                  __LINE__, THLA_NEWLINE );
      }

      try {
         RTI_ambassador->disableTimeConstrained();
         time_constrained_state = false;
      } catch ( RTI1516_NAMESPACE::TimeConstrainedIsNotEnabled &e ) {
         time_constrained_state = false;
         send_hs( stderr, "Federate::shutdown_time_constrained():%d \"%s\": TimeConstrainedIsNotEnabled EXCEPTION!%c",
                  __LINE__, get_federation_name(), THLA_NEWLINE );
      } catch ( RTI1516_NAMESPACE::FederateNotExecutionMember &e ) {
         time_constrained_state = false;
         send_hs( stderr, "Federate::shutdown_time_constrained():%d \"%s\": FederateNotExecutionMember EXCEPTION!%c",
                  __LINE__, get_federation_name(), THLA_NEWLINE );
      } catch ( RTI1516_NAMESPACE::SaveInProgress &e ) {
         send_hs( stderr, "Federate::shutdown_time_constrained():%d \"%s\": SaveInProgress EXCEPTION!%c",
                  __LINE__, get_federation_name(), THLA_NEWLINE );
      } catch ( RTI1516_NAMESPACE::RestoreInProgress &e ) {
         send_hs( stderr, "Federate::shutdown_time_constrained():%d \"%s\": RestoreInProgress EXCEPTION!%c",
                  __LINE__, get_federation_name(), THLA_NEWLINE );
      } catch ( RTI1516_NAMESPACE::NotConnected &e ) {
         time_constrained_state = false;
         send_hs( stderr, "Federate::shutdown_time_constrained():%d \"%s\": NotConnected EXCEPTION!%c",
                  __LINE__, get_federation_name(), THLA_NEWLINE );
      } catch ( RTI1516_NAMESPACE::RTIinternalError &e ) {
         string rti_err_msg;
         StringUtilities::to_string( rti_err_msg, e.what() );
         send_hs( stderr, "Federate::shutdown_time_constrained():%d \"%s\": RTIinternalError EXCEPTION: '%s'%c",
                  __LINE__, get_federation_name(), rti_err_msg.c_str(), THLA_NEWLINE );
      } catch ( RTI1516_EXCEPTION &e ) {
         send_hs( stderr, "Federate::shutdown_time_constrained():%d \"%s\": Unexpected RTI EXCEPTION!%c",
                  __LINE__, get_federation_name(), THLA_NEWLINE );
      }

      // Macro to restore the saved FPU Control Word register value.
      TRICKHLA_RESTORE_FPU_CONTROL_WORD;
      TRICKHLA_VALIDATE_FPU_CONTROL_WORD;
   }
}

/*!
 *  @job_class{shutdown}
 */
void Federate::shutdown_time_regulating()
{
   if ( !time_regulating_state ) {
      if ( should_print( DEBUG_LEVEL_2_TRACE, DEBUG_SOURCE_FEDERATE ) ) {
         send_hs( stdout, "Federate::shutdown_time_regulating():%d HLA Time Regulation Already Disabled.%c",
                  __LINE__, THLA_NEWLINE );
      }
   } else {
      // Macro to save the FPU Control Word register value.
      TRICKHLA_SAVE_FPU_CONTROL_WORD;

      // Make sure we've been able to get the RTI ambassador.
      if ( RTI_ambassador.get() == NULL ) {
         return;
      }

      if ( should_print( DEBUG_LEVEL_2_TRACE, DEBUG_SOURCE_FEDERATE ) ) {
         send_hs( stdout, "Federate::shutdown_time_regulating():%d Disabling HLA Time Regulation.%c",
                  __LINE__, THLA_NEWLINE );
      }

      try {
         RTI_ambassador->disableTimeRegulation();
         time_regulating_state = false;
      } catch ( RTI1516_NAMESPACE::TimeConstrainedIsNotEnabled &e ) {
         time_regulating_state = false;
         send_hs( stderr, "Federate::shutdown_time_regulating():%d \"%s\": TimeConstrainedIsNotEnabled EXCEPTION!%c",
                  __LINE__, get_federation_name(), THLA_NEWLINE );
      } catch ( RTI1516_NAMESPACE::FederateNotExecutionMember &e ) {
         time_regulating_state = false;
         send_hs( stderr, "Federate::shutdown_time_regulating():%d \"%s\": FederateNotExecutionMember EXCEPTION!%c",
                  __LINE__, get_federation_name(), THLA_NEWLINE );
      } catch ( RTI1516_NAMESPACE::SaveInProgress &e ) {
         send_hs( stderr, "Federate::shutdown_time_regulating():%d \"%s\": SaveInProgress EXCEPTION!%c",
                  __LINE__, get_federation_name(), THLA_NEWLINE );
      } catch ( RTI1516_NAMESPACE::RestoreInProgress &e ) {
         send_hs( stderr, "Federate::shutdown_time_regulating():%d \"%s\": RestoreInProgress EXCEPTION!%c",
                  __LINE__, get_federation_name(), THLA_NEWLINE );
      } catch ( RTI1516_NAMESPACE::NotConnected &e ) {
         time_constrained_state = false;
         send_hs( stderr, "Federate::shutdown_time_regulating():%d \"%s\": NotConnected EXCEPTION!%c",
                  __LINE__, get_federation_name(), THLA_NEWLINE );
      } catch ( RTI1516_NAMESPACE::RTIinternalError &e ) {
         string rti_err_msg;
         StringUtilities::to_string( rti_err_msg, e.what() );
         send_hs( stderr, "Federate::shutdown_time_regulating():%d \"%s\": RTIinternalError EXCEPTION: '%s'%c",
                  __LINE__, get_federation_name(), rti_err_msg.c_str(), THLA_NEWLINE );
      } catch ( RTI1516_EXCEPTION &e ) {
         send_hs( stderr, "Federate::shutdown_time_regulating():%d \"%s\": Unexpected RTI EXCEPTION!%c",
                  __LINE__, get_federation_name(), THLA_NEWLINE );
      }

      // Macro to restore the saved FPU Control Word register value.
      TRICKHLA_RESTORE_FPU_CONTROL_WORD;
      TRICKHLA_VALIDATE_FPU_CONTROL_WORD;
   }
}

/*!
 *  @job_class{shutdown}
 */
void Federate::resign()
{
   // Macro to save the FPU Control Word register value.
   TRICKHLA_SAVE_FPU_CONTROL_WORD;

   // Make sure we've been able to set the RTI ambassador.
   if ( RTI_ambassador.get() == NULL ) {
      // Macro to restore the saved FPU Control Word register value.
      TRICKHLA_RESTORE_FPU_CONTROL_WORD;
      TRICKHLA_VALIDATE_FPU_CONTROL_WORD;
      return;
   }
   // Resign from the federation execution to remove this federate from
   // participation. The flag provided will instruct the RTI to call
   // deleteObjectInstance for all objects this federate has the
   // privilegeToDelete for (which by default is all objects that this
   // federate registered) and to release ownership of any attributes that
   // this federate owns but does not own the privilegeToDelete for.
   try {
      if ( should_print( DEBUG_LEVEL_4_TRACE, DEBUG_SOURCE_FEDERATE ) ) {
         send_hs( stdout, "Federate::resign():%d Attempting to resign from Federation '%s'%c",
                  __LINE__, get_federation_name(), THLA_NEWLINE );
      }

      RTI_ambassador->resignFederationExecution( RTI1516_NAMESPACE::CANCEL_THEN_DELETE_THEN_DIVEST );

      this->federation_joined = false;

      if ( should_print( DEBUG_LEVEL_2_TRACE, DEBUG_SOURCE_FEDERATE ) ) {
         send_hs( stdout, "Federate::resign():%d Resigned from Federation '%s'%c",
                  __LINE__, get_federation_name(), THLA_NEWLINE );
      }
   } catch ( InvalidResignAction &e ) {
      // Macro to restore the saved FPU Control Word register value.
      TRICKHLA_RESTORE_FPU_CONTROL_WORD;
      TRICKHLA_VALIDATE_FPU_CONTROL_WORD;

      ostringstream errmsg;
      errmsg << "Federate::resign():" << __LINE__
             << " Failed to resign Federate from the '"
             << get_federation_name()
             << "' Federation because it received an EXCEPTION: "
             << "InvalidResignAction" << THLA_ENDL;

      send_hs( stderr, (char *)errmsg.str().c_str() );
      exec_terminate( __FILE__, (char *)errmsg.str().c_str() );
   } catch ( OwnershipAcquisitionPending &e ) {
      // Macro to restore the saved FPU Control Word register value.
      TRICKHLA_RESTORE_FPU_CONTROL_WORD;
      TRICKHLA_VALIDATE_FPU_CONTROL_WORD;

      ostringstream errmsg;
      errmsg << "Federate::resign():" << __LINE__
             << " Failed to resign Federate from the '"
             << get_federation_name()
             << "' Federation because it received an EXCEPTION: "
             << "OwnershipAcquisitionPending" << THLA_ENDL;

      send_hs( stderr, (char *)errmsg.str().c_str() );
      exec_terminate( __FILE__, (char *)errmsg.str().c_str() );
   } catch ( FederateOwnsAttributes &e ) {
      // Macro to restore the saved FPU Control Word register value.
      TRICKHLA_RESTORE_FPU_CONTROL_WORD;
      TRICKHLA_VALIDATE_FPU_CONTROL_WORD;

      ostringstream errmsg;
      errmsg << "Federate::resign():" << __LINE__
             << " Failed to resign Federate from the '"
             << get_federation_name()
             << "' Federation because it received an EXCEPTION: "
             << "FederateOwnsAttributes";

      send_hs( stderr, (char *)errmsg.str().c_str() );
      exec_terminate( __FILE__, (char *)errmsg.str().c_str() );
   } catch ( FederateNotExecutionMember &e ) {
      // Macro to restore the saved FPU Control Word register value.
      TRICKHLA_RESTORE_FPU_CONTROL_WORD;
      TRICKHLA_VALIDATE_FPU_CONTROL_WORD;

      this->federation_joined = false;

      ostringstream errmsg;
      errmsg << "Federate::resign():" << __LINE__
             << " Failed to resign Federate from the '"
             << get_federation_name()
             << "' Federation because it received an EXCEPTION: "
             << "FederateNotExecutionMember" << THLA_ENDL;

      send_hs( stderr, (char *)errmsg.str().c_str() );
   } catch ( NotConnected &e ) {
      // Macro to restore the saved FPU Control Word register value.
      TRICKHLA_RESTORE_FPU_CONTROL_WORD;
      TRICKHLA_VALIDATE_FPU_CONTROL_WORD;

      this->federation_joined = false;

      ostringstream errmsg;
      errmsg << "Federate::resign():" << __LINE__
             << " Failed to resign Federate from the '"
             << get_federation_name()
             << "' Federation because it received an EXCEPTION: "
             << "NotConnected" << THLA_ENDL;

      // Just display an error message and don't terminate if we are not connected.
      send_hs( stderr, (char *)errmsg.str().c_str() );
   } catch ( CallNotAllowedFromWithinCallback &e ) {
      // Macro to restore the saved FPU Control Word register value.
      TRICKHLA_RESTORE_FPU_CONTROL_WORD;
      TRICKHLA_VALIDATE_FPU_CONTROL_WORD;

      ostringstream errmsg;
      errmsg << "Federate::resign():" << __LINE__
             << " Failed to resign Federate from the '"
             << get_federation_name()
             << "' Federation because it received an EXCEPTION: "
             << "CallNotAllowedFromWithinCallback" << THLA_ENDL;

      send_hs( stderr, (char *)errmsg.str().c_str() );
      exec_terminate( __FILE__, (char *)errmsg.str().c_str() );
   } catch ( RTIinternalError &e ) {
      // Macro to restore the saved FPU Control Word register value.
      TRICKHLA_RESTORE_FPU_CONTROL_WORD;
      TRICKHLA_VALIDATE_FPU_CONTROL_WORD;

      string rti_err_msg;
      StringUtilities::to_string( rti_err_msg, e.what() );
      ostringstream errmsg;
      errmsg << "Federate::resign():" << __LINE__
             << " Failed to resign Federate from the '"
             << get_federation_name()
             << "' Federation because of the RTIinternalError: "
             << rti_err_msg << THLA_ENDL;

      send_hs( stderr, (char *)errmsg.str().c_str() );
      exec_terminate( __FILE__, (char *)errmsg.str().c_str() );
   } catch ( RTI1516_EXCEPTION &e ) {
      // Macro to restore the saved FPU Control Word register value.
      TRICKHLA_RESTORE_FPU_CONTROL_WORD;
      TRICKHLA_VALIDATE_FPU_CONTROL_WORD;

      string rti_err_msg;
      StringUtilities::to_string( rti_err_msg, e.what() );
      ostringstream errmsg;
      errmsg << "Federate::resign():" << __LINE__
             << " Failed to resign Federate from the '"
             << get_federation_name()
             << "' Federation because of the RTI Exception: "
             << rti_err_msg << THLA_ENDL;

      send_hs( stderr, (char *)errmsg.str().c_str() );
      exec_terminate( __FILE__, (char *)errmsg.str().c_str() );
   }
   // Macro to restore the saved FPU Control Word register value.
   TRICKHLA_RESTORE_FPU_CONTROL_WORD;
   TRICKHLA_VALIDATE_FPU_CONTROL_WORD;
}

/*!
 *  @details Resign from the federation but divest ownership of my attributes
 *  and do not delete the federate from the federation when resigning.
 *  @job_class{logging}
 */
void Federate::resign_so_we_can_rejoin()
{
   // Macro to save the FPU Control Word register value.
   TRICKHLA_SAVE_FPU_CONTROL_WORD;

   // Make sure we've been able to set the RTI ambassador.
   if ( RTI_ambassador.get() == NULL ) {
      // Macro to restore the saved FPU Control Word register value.
      TRICKHLA_RESTORE_FPU_CONTROL_WORD;
      TRICKHLA_VALIDATE_FPU_CONTROL_WORD;
      return;
   }

   try {
      if ( should_print( DEBUG_LEVEL_2_TRACE, DEBUG_SOURCE_FEDERATE ) ) {
         send_hs( stdout, "Federate::resign_so_we_can_rejoin():%d \
Federation \"%s\": RESIGNING FROM FEDERATION (with the ability to rejoin federation)%c",
                  __LINE__, get_federation_name(), THLA_NEWLINE );
      }

      RTI_ambassador->resignFederationExecution( RTI1516_NAMESPACE::UNCONDITIONALLY_DIVEST_ATTRIBUTES );

      this->federation_joined = false;

   } catch ( InvalidResignAction &e ) {
      // Macro to restore the saved FPU Control Word register value.
      TRICKHLA_RESTORE_FPU_CONTROL_WORD;
      TRICKHLA_VALIDATE_FPU_CONTROL_WORD;

      ostringstream errmsg;
      errmsg << "Federate::resign_so_we_can_rejoin():" << __LINE__
             << " Failed to resign Federate from the '"
             << get_federation_name()
             << "' Federation because it received an EXCEPTION: "
             << "InvalidResignAction" << THLA_ENDL;

      send_hs( stderr, (char *)errmsg.str().c_str() );
      exec_terminate( __FILE__, (char *)errmsg.str().c_str() );
   } catch ( OwnershipAcquisitionPending &e ) {
      // Macro to restore the saved FPU Control Word register value.
      TRICKHLA_RESTORE_FPU_CONTROL_WORD;
      TRICKHLA_VALIDATE_FPU_CONTROL_WORD;

      ostringstream errmsg;
      errmsg << "Federate::resign_so_we_can_rejoin():" << __LINE__
             << " Failed to resign Federate from the '"
             << get_federation_name()
             << "' Federation because it received an EXCEPTION: "
             << "OwnershipAcquisitionPending" << THLA_ENDL;

      send_hs( stderr, (char *)errmsg.str().c_str() );
      exec_terminate( __FILE__, (char *)errmsg.str().c_str() );
   } catch ( FederateOwnsAttributes &e ) {
      ostringstream errmsg;
      errmsg << "Federate::resign_so_we_can_rejoin():" << __LINE__
             << " Failed to resign Federate from the '"
             << get_federation_name()
             << "' Federation received an EXCEPTION: "
             << "FederateOwnsAttributes" << THLA_ENDL;

      send_hs( stdout, (char *)errmsg.str().c_str() );
   } catch ( FederateNotExecutionMember &e ) {
      // Macro to restore the saved FPU Control Word register value.
      TRICKHLA_RESTORE_FPU_CONTROL_WORD;
      TRICKHLA_VALIDATE_FPU_CONTROL_WORD;

      ostringstream errmsg;
      errmsg << "Federate::resign_so_we_can_rejoin():" << __LINE__
             << " Failed to resign Federate from the '"
             << get_federation_name()
             << "' Federation because it received an EXCEPTION: "
             << "FederateNotExecutionMember" << THLA_ENDL;

      send_hs( stderr, (char *)errmsg.str().c_str() );
      exec_terminate( __FILE__, (char *)errmsg.str().c_str() );
   } catch ( NotConnected &e ) {
      // Macro to restore the saved FPU Control Word register value.
      TRICKHLA_RESTORE_FPU_CONTROL_WORD;
      TRICKHLA_VALIDATE_FPU_CONTROL_WORD;

      ostringstream errmsg;
      errmsg << "Federate::resign_so_we_can_rejoin():" << __LINE__
             << " Failed to resign Federate from the '"
             << get_federation_name()
             << "' Federation because it received an EXCEPTION: "
             << "NotConnected" << THLA_ENDL;

      send_hs( stderr, (char *)errmsg.str().c_str() );
      exec_terminate( __FILE__, (char *)errmsg.str().c_str() );
   } catch ( CallNotAllowedFromWithinCallback &e ) {
      // Macro to restore the saved FPU Control Word register value.
      TRICKHLA_RESTORE_FPU_CONTROL_WORD;
      TRICKHLA_VALIDATE_FPU_CONTROL_WORD;

      ostringstream errmsg;
      errmsg << "Federate::resign_so_we_can_rejoin():" << __LINE__
             << " Failed to resign Federate from the '"
             << get_federation_name()
             << "' Federation because it received an EXCEPTION: "
             << "CallNotAllowedFromWithinCallback" << THLA_ENDL;

      send_hs( stderr, (char *)errmsg.str().c_str() );
      exec_terminate( __FILE__, (char *)errmsg.str().c_str() );
   } catch ( RTIinternalError &e ) {
      // Macro to restore the saved FPU Control Word register value.
      TRICKHLA_RESTORE_FPU_CONTROL_WORD;
      TRICKHLA_VALIDATE_FPU_CONTROL_WORD;

      string rti_err_msg;
      StringUtilities::to_string( rti_err_msg, e.what() );
      ostringstream errmsg;
      errmsg << "Federate::resign_so_we_can_rejoin():" << __LINE__
             << " Failed to resign Federate from the '"
             << get_federation_name()
             << "' Federation because of the RTIinternalError: "
             << rti_err_msg << THLA_ENDL;

      send_hs( stderr, (char *)errmsg.str().c_str() );
      exec_terminate( __FILE__, (char *)errmsg.str().c_str() );
   } catch ( RTI1516_EXCEPTION &e ) {
      // Macro to restore the saved FPU Control Word register value.
      TRICKHLA_RESTORE_FPU_CONTROL_WORD;
      TRICKHLA_VALIDATE_FPU_CONTROL_WORD;

      string rti_err_msg;
      StringUtilities::to_string( rti_err_msg, e.what() );
      ostringstream errmsg;
      errmsg << "Federate::resign_so_we_can_rejoin():" << __LINE__
             << " Failed to resign Federate from the '"
             << get_federation_name()
             << "' Federation because of the RTI Exception: "
             << rti_err_msg << THLA_ENDL;

      send_hs( stderr, (char *)errmsg.str().c_str() );
      exec_terminate( __FILE__, (char *)errmsg.str().c_str() );
   }

   // Macro to restore the saved FPU Control Word register value.
   TRICKHLA_RESTORE_FPU_CONTROL_WORD;
   TRICKHLA_VALIDATE_FPU_CONTROL_WORD;

   // TODO: Do we really want to exec_terminate here! DDexter 9/27/2010
   ostringstream msg;
   msg << "Federate::resign_so_we_can_rejoin():" << __LINE__
       << " Federate '" << get_federate_name()
       << "' resigned from Federation '" << get_federation_name() << "'"
       << THLA_ENDL;
   send_hs( stdout, (char *)msg.str().c_str() );
   exec_terminate( __FILE__, (char *)msg.str().c_str() );
}

/*!
 *  @job_class{shutdown}
 */
void Federate::destroy()
{
   // Macro to save the FPU Control Word register value.
   TRICKHLA_SAVE_FPU_CONTROL_WORD;

   // Destroy the federation execution in case we are the last federate. This
   // will not do anything bad if there other federates joined. The RTI will
   // throw us an exception telling us that other federates are joined and we
   // can just ignore that.
   if ( RTI_ambassador.get() == NULL ) {
      // Macro to restore the saved FPU Control Word register value.
      TRICKHLA_RESTORE_FPU_CONTROL_WORD;
      TRICKHLA_VALIDATE_FPU_CONTROL_WORD;
      return;
   }

   // Create the wide-string version of the federation name.
   wstring federation_name_ws;
   StringUtilities::to_wstring( federation_name_ws, federation_name );

   try {
      if ( should_print( DEBUG_LEVEL_4_TRACE, DEBUG_SOURCE_FEDERATE ) ) {
         send_hs( stdout, "Federate::destroy():%d Attempting to Destroy Federation '%s'%c",
                  __LINE__, get_federation_name(), THLA_NEWLINE );
      }

      RTI_ambassador->destroyFederationExecution( federation_name_ws );

      this->federation_exists = false;
      this->federation_joined = false;

      if ( should_print( DEBUG_LEVEL_2_TRACE, DEBUG_SOURCE_FEDERATE ) ) {
         send_hs( stdout, "Federate::destroy():%d Destroyed Federation '%s'%c",
                  __LINE__, get_federation_name(), THLA_NEWLINE );
      }
   } catch ( RTI1516_NAMESPACE::FederatesCurrentlyJoined &e ) {
      // Macro to restore the saved FPU Control Word register value.
      TRICKHLA_RESTORE_FPU_CONTROL_WORD;
      TRICKHLA_VALIDATE_FPU_CONTROL_WORD;

      this->federation_joined = false;

      // Put this warning message at a higher trace level since every
      // federate that is not the last one in the federation will see this
      // message when they try to destroy the federation. This is expected.
      if ( should_print( DEBUG_LEVEL_4_TRACE, DEBUG_SOURCE_FEDERATE ) ) {
         send_hs( stderr, "Federate::destroy():%d Federation '%s' destroy failed because this is not the last federate, which is expected.%c",
                  __LINE__, get_federation_name(), THLA_NEWLINE );
      }
   } catch ( RTI1516_NAMESPACE::FederationExecutionDoesNotExist &e ) {
      // Macro to restore the saved FPU Control Word register value.
      TRICKHLA_RESTORE_FPU_CONTROL_WORD;
      TRICKHLA_VALIDATE_FPU_CONTROL_WORD;

      this->federation_exists = false;
      this->federation_joined = false;

      if ( should_print( DEBUG_LEVEL_2_TRACE, DEBUG_SOURCE_FEDERATE ) ) {
         send_hs( stderr, "Federate::destroy():%d Federation '%s' Already Destroyed.%c",
                  __LINE__, get_federation_name(), THLA_NEWLINE );
      }
   } catch ( RTI1516_NAMESPACE::NotConnected &e ) {
      // Macro to restore the saved FPU Control Word register value.
      TRICKHLA_RESTORE_FPU_CONTROL_WORD;
      TRICKHLA_VALIDATE_FPU_CONTROL_WORD;

      this->federation_exists = false;
      this->federation_joined = false;

      if ( should_print( DEBUG_LEVEL_2_TRACE, DEBUG_SOURCE_FEDERATE ) ) {
         send_hs( stderr, "Federate::destroy():%d Federation '%s' destroy failed because we are NOT CONNECTED to the federation.%c",
                  __LINE__, get_federation_name(), THLA_NEWLINE );
      }
   } catch ( RTI1516_EXCEPTION &e ) {
      // Macro to restore the saved FPU Control Word register value.
      TRICKHLA_RESTORE_FPU_CONTROL_WORD;
      TRICKHLA_VALIDATE_FPU_CONTROL_WORD;

      string rti_err_msg;
      StringUtilities::to_string( rti_err_msg, e.what() );
      ostringstream errmsg;
      errmsg << "Federate::destroy():" << __LINE__
             << " Federation '" << get_federation_name()
             << "': Unexpected RTI exception when destroying federation!\n"
             << "RTI Exception: RTIinternalError: '"
             << rti_err_msg << "'" << THLA_ENDL;

      send_hs( stderr, (char *)errmsg.str().c_str() );
      exec_terminate( __FILE__, (char *)errmsg.str().c_str() );
   }

   try {
      if ( should_print( DEBUG_LEVEL_4_TRACE, DEBUG_SOURCE_FEDERATE ) ) {
         send_hs( stdout, "Federate::destroy():%d Attempting to disconnect from RTI %c",
                  __LINE__, THLA_NEWLINE );
      }

      RTI_ambassador->disconnect();

      this->federation_exists = false;
      this->federation_joined = false;

      if ( should_print( DEBUG_LEVEL_2_TRACE, DEBUG_SOURCE_FEDERATE ) ) {
         send_hs( stdout, "Federate::destroy():%d Disconnected from RTI %c",
                  __LINE__, THLA_NEWLINE );
      }
   } catch ( RTI1516_NAMESPACE::FederateIsExecutionMember &e ) {
      // Macro to restore the saved FPU Control Word register value.
      TRICKHLA_RESTORE_FPU_CONTROL_WORD;
      TRICKHLA_VALIDATE_FPU_CONTROL_WORD;

      if ( should_print( DEBUG_LEVEL_4_TRACE, DEBUG_SOURCE_FEDERATE ) ) {
         send_hs( stderr, "Federate::destroy():%d Cannot disconnect from RTI because this federate is still joined.%c",
                  __LINE__, THLA_NEWLINE );
      }
   } catch ( RTI1516_EXCEPTION &e ) {
      // Macro to restore the saved FPU Control Word register value.
      TRICKHLA_RESTORE_FPU_CONTROL_WORD;
      TRICKHLA_VALIDATE_FPU_CONTROL_WORD;

      string rti_err_msg;
      StringUtilities::to_string( rti_err_msg, e.what() );
      ostringstream errmsg;
      errmsg << "Federate::destroy():" << __LINE__
             << " Unexpected RTI exception when disconnecting from RTI!\n"
             << "RTI Exception: RTIinternalError: '"
             << rti_err_msg << "'" << THLA_ENDL;

      send_hs( stderr, (char *)errmsg.str().c_str() );
      exec_terminate( __FILE__, (char *)errmsg.str().c_str() );
   }

   // Macro to restore the saved FPU Control Word register value.
   TRICKHLA_RESTORE_FPU_CONTROL_WORD;
   TRICKHLA_VALIDATE_FPU_CONTROL_WORD;
}

/*!
 *  @job_class{initialization}
 */
void Federate::destroy_orphaned_federation()
{
#ifdef PORTICO_RTI
   // The Portico RTI will close the connection to the RTI when we try to delete
   // an orphaned federation, so just skip this step as a workaround.
   send_hs( stdout, "Federate::destroy_orphaned_federation():%d WARNING: Portico RTI will close the connection, skipping...%c",
            __LINE__, THLA_NEWLINE );
   return;
#endif

   // Macro to save the FPU Control Word register value.
   TRICKHLA_SAVE_FPU_CONTROL_WORD;

   // Print an error message if the RTI ambassador is NULL.
   if ( RTI_ambassador.get() == NULL ) {
      send_hs( stderr, "Federate::destroy_orphaned_federation():%d Unexpected NULL RTIambassador.%c",
               __LINE__, THLA_NEWLINE );
      exec_terminate( __FILE__, "Federate::destroy_orphaned_federation() Unexpected NULL RTIambassador." );
   }

   // Create the wide-string version of the federation name.
   wstring federation_name_ws;
   StringUtilities::to_wstring( federation_name_ws, federation_name );

   if ( should_print( DEBUG_LEVEL_9_TRACE, DEBUG_SOURCE_FEDERATE ) ) {
      send_hs( stdout, "Federate::destroy_orphaned_federation():%d Attempting to Destroy Orphaned Federation '%s'.%c",
               __LINE__, get_federation_name(), THLA_NEWLINE );
   }

   try {
      RTI_ambassador->destroyFederationExecution( federation_name_ws );

      // If we don't get an exception then we successfully destroyed
      // an orphaned federation.
      if ( should_print( DEBUG_LEVEL_2_TRACE, DEBUG_SOURCE_FEDERATE ) ) {
         send_hs( stdout, "Federate::destroy_orphaned_federation():%d Successfully Destroyed Orphaned Federation '%s'.%c",
                  __LINE__, get_federation_name(), THLA_NEWLINE );
      }
   } catch ( RTI1516_EXCEPTION &e ) {
      // Ignore any exception since we are just removing an orphaned federation.
   }
   // Macro to restore the saved FPU Control Word register value.
   TRICKHLA_RESTORE_FPU_CONTROL_WORD;
   TRICKHLA_VALIDATE_FPU_CONTROL_WORD;
}

/*!
 *  @job_class{initialization}
 */
void Federate::set_federation_name(
   const char *const exec_name )
{

   // Check for self assign.
   if ( federation_name != exec_name ) {

      // Check for "hard coded" name.
      if ( exec_name != NULL ) {

         // Reallocate and set the federation execution name.
         if ( federation_name != static_cast< char * >( NULL ) ) {
            if ( TMM_is_alloced( federation_name ) ) {
               TMM_delete_var_a( federation_name );
            }
            federation_name = static_cast< char * >( NULL );
         }

         // Set the federation execution name.
         federation_name = TMM_strdup( (char *)exec_name );
      } else {

         // Set to a default value if not alread set in the input stream.
         if ( federation_name == static_cast< char * >( NULL ) ) {
            federation_name = TMM_strdup( (char *)"Trick Federation" );
         }
      }
   }
}

void Federate::ask_MOM_for_auto_provide_setting()
{
   if ( should_print( DEBUG_LEVEL_2_TRACE, DEBUG_SOURCE_FEDERATE ) ) {
      send_hs( stdout, "Federate::ask_MOM_for_auto_provide_setting():%d%c",
               __LINE__, THLA_NEWLINE );
   }

   // Make sure the MOM handles get initialized before we try to use them.
   if ( !MOM_HLAautoProvide_handle.isValid() ) {
      initialize_MOM_handles();
   }

   // Reset the value to an unknown state so that we will know when we get the
   // actual value from the MOM.
   this->auto_provide_setting = -1;

   // Use the MOM to get the list of registered federates.
   AttributeHandleSet fedMomAttributes;
   fedMomAttributes.insert( MOM_HLAautoProvide_handle );
   subscribe_attributes( MOM_HLAfederation_class_handle, fedMomAttributes );

   AttributeHandleSet requestedAttributes;
   requestedAttributes.insert( MOM_HLAautoProvide_handle );
   request_attribute_update( MOM_HLAfederation_class_handle, requestedAttributes );

   unsigned int sleep_micros = 1000;
   unsigned int wait_count   = 0;
   unsigned int wait_check   = 10000000 / sleep_micros; // Number of wait cycles for 10 seconds

   while ( auto_provide_setting < 0 ) {

      // Check for shutdown.
      this->check_for_shutdown_with_termination();

      // Sleep a little while to wait for the information to update.
      usleep( sleep_micros );

      if ( ( auto_provide_setting < 0 ) && ( ( ++wait_count % wait_check ) == 0 ) ) {
         wait_count = 0;
         if ( !is_execution_member() ) {
            ostringstream errmsg;
            errmsg << "Federate::ask_MOM_for_auto_provide_setting():" << __LINE__
                   << " Unexpectedly the Federate is no longer an execution member."
                   << " This means we are either not connected to the"
                   << " RTI or we are no longer joined to the federation"
                   << " execution because someone forced our resignation at"
                   << " the Central RTI Component (CRC) level!"
                   << THLA_ENDL;
            send_hs( stderr, (char *)errmsg.str().c_str() );
            exec_terminate( __FILE__, (char *)errmsg.str().c_str() );
         }
      }
   }

   // Only unsubscribe from the attributes we subscribed to in this function.
   unsubscribe_attributes( MOM_HLAfederation_class_handle, fedMomAttributes );

   if ( should_print( DEBUG_LEVEL_2_TRACE, DEBUG_SOURCE_FEDERATE ) ) {
      send_hs( stdout, "Federate::ask_MOM_for_auto_provide_setting():%d Auto-Provide:%s value:%d%c",
               __LINE__, ( ( auto_provide_setting != 0 ) ? "Yes" : "No" ),
               auto_provide_setting, THLA_NEWLINE );
   }

   return;
}

void Federate::enable_MOM_auto_provide_setting(
   bool enable )
{
   // Keep the auto-provide setting in sync with our enable request and set the
   // Big Endian value the RTI expects for the auto-provide setting.
   int requested_auto_provide;
   if ( enable ) {
      this->auto_provide_setting = 1;
      // 1 as 32-bit Big Endian as required for the HLAautoProvide parameter.
      requested_auto_provide = Utilities::is_transmission_byteswap( ENCODING_BIG_ENDIAN ) ? Utilities::byteswap_int( 1 ) : 1;
   } else {
      this->auto_provide_setting = 0;
      requested_auto_provide     = 0;
   }

   if ( should_print( DEBUG_LEVEL_2_TRACE, DEBUG_SOURCE_FEDERATE ) ) {
      send_hs( stdout, "Federate::enable_MOM_auto_provide_setting():%d Auto-Provide:%s%c",
               __LINE__, ( enable ? "Yes" : "No" ), THLA_NEWLINE );
   }

   publish_interaction_class( MOM_HLAsetSwitches_class_handle );

   ParameterHandleValueMap param_values_map;
   param_values_map[MOM_HLAautoProvide_param_handle] =
      VariableLengthData( &requested_auto_provide, sizeof( requested_auto_provide ) );

   send_interaction( MOM_HLAsetSwitches_class_handle, param_values_map );

   unpublish_interaction_class( MOM_HLAsetSwitches_class_handle );

   return;
}

void Federate::backup_auto_provide_setting_from_MOM_then_disable()
{
   if ( should_print( DEBUG_LEVEL_2_TRACE, DEBUG_SOURCE_FEDERATE ) ) {
      send_hs( stdout, "Federate::backup_auto_provide_setting_from_MOM_then_disable():%d%c",
               __LINE__, THLA_NEWLINE );
   }

   ask_MOM_for_auto_provide_setting();

   // Backup the original auto-provide setting.
   this->orig_auto_provide_setting = this->auto_provide_setting;

   // Disable Auto-Provide if it is enabled.
   if ( auto_provide_setting != 0 ) {
      enable_MOM_auto_provide_setting( false );
   }

   return;
}

void Federate::restore_orig_MOM_auto_provide_setting()
{

   // Only update the auto-provide setting if the original setting does not
   // match the current setting.
   if ( auto_provide_setting != orig_auto_provide_setting ) {
      if ( should_print( DEBUG_LEVEL_2_TRACE, DEBUG_SOURCE_FEDERATE ) ) {
         send_hs( stdout, "Federate::restore_orig_MOM_auto_provide_setting():%d Auto-Provide:%s%c",
                  __LINE__, ( ( orig_auto_provide_setting != 0 ) ? "Yes" : "No" ), THLA_NEWLINE );
      }
      enable_MOM_auto_provide_setting( orig_auto_provide_setting != 0 );
   }

   return;
}

//**************************************************************************
//**************************************************************************
//*************** START OF CHECKPOINT / RESTORE CODE ***********************
//**************************************************************************
//**************************************************************************

void Federate::load_and_print_running_federate_names()
{
   if ( should_print( DEBUG_LEVEL_2_TRACE, DEBUG_SOURCE_FEDERATE ) ) {
      send_hs( stdout, "Federate::load_and_print_running_federate_names():%d started.%c",
               __LINE__, THLA_NEWLINE );
   }

   // Make sure the MOM handles get initialized before we try to use them.
   if ( !MOM_HLAfederation_class_handle.isValid() ) {
      initialize_MOM_handles();
   }

   AttributeHandleSet fedMomAttributes;
   fedMomAttributes.insert( MOM_HLAfederatesInFederation_handle );
   subscribe_attributes( MOM_HLAfederation_class_handle, fedMomAttributes );

   AttributeHandleSet requestedAttributes;
   requestedAttributes.insert( MOM_HLAfederatesInFederation_handle );
   request_attribute_update( MOM_HLAfederation_class_handle, requestedAttributes );

   unsigned int sleep_micros = 1000;
   unsigned int wait_count   = 0;
   unsigned int wait_check   = 10000000 / sleep_micros; // Number of wait cycles for 10 seconds

   while ( running_feds_count <= 0 ) {

      // Check for shutdown.
      this->check_for_shutdown_with_termination();

      // Sleep a little while to wait for the information to update.
      usleep( sleep_micros );

      if ( ( running_feds_count <= 0 ) && ( ( ++wait_count % wait_check ) == 0 ) ) {
         wait_count = 0;
         if ( !is_execution_member() ) {
            ostringstream errmsg;
            errmsg << "Federate::load_and_print_running_federate_names():" << __LINE__
                   << " Unexpectedly the Federate is no longer an execution member."
                   << " This means we are either not connected to the"
                   << " RTI or we are no longer joined to the federation"
                   << " execution because someone forced our resignation at"
                   << " the Central RTI Component (CRC) level!"
                   << THLA_ENDL;
            send_hs( stderr, (char *)errmsg.str().c_str() );
            exec_terminate( __FILE__, (char *)errmsg.str().c_str() );
         }
      }
   }

   // Only unsubscribe from the attributes we subscribed to in this function.
   unsubscribe_attributes( MOM_HLAfederation_class_handle, fedMomAttributes );

   if ( should_print( DEBUG_LEVEL_2_TRACE, DEBUG_SOURCE_FEDERATE ) ) {
      send_hs( stdout, "Federate::load_and_print_running_federate_names():%d \
MOM just informed us that there are %d federates currently running in the federation.%c",
               __LINE__, running_feds_count, THLA_NEWLINE );
   }

   // Also, clear out the previous list of joined federates... this data is NOT
   // checkpointed, right? Besides, this collection needs to be wiped out since
   // it is the loop driver for the joined elements later in the code...
   joined_federate_names.clear();

   // ==> Now, execute code lifted from wait_for_required_federates_to_join... <===

   // Make sure we clear the joined federate handle set.
   joined_federate_handles.clear();

   ask_MOM_for_federate_names();

   size_t joinedFedCount = 0;

   // Wait for all the required federates to join.
   wait_count           = 0;
   all_federates_joined = false;
   while ( !all_federates_joined ) {

      // Check for shutdown.
      this->check_for_shutdown_with_termination();

      // Sleep a little while to wait for more federates to join.
      usleep( sleep_micros );

      // Determine what federates have joined only if the joined federate
      // count has changed.
      if ( joinedFedCount != joined_federate_names.size() ) {
         joinedFedCount = joined_federate_names.size();

         if ( joinedFedCount >= (unsigned int)running_feds_count ) {
            all_federates_joined = true;
         }
      }
      if ( ( !all_federates_joined ) && ( ( ++wait_count % wait_check ) == 0 ) ) {
         wait_count = 0;
         if ( !is_execution_member() ) {
            ostringstream errmsg;
            errmsg << "Federate::load_and_print_running_federate_names():" << __LINE__
                   << " Unexpectedly the Federate is no longer an execution member."
                   << " This means we are either not connected to the"
                   << " RTI or we are no longer joined to the federation"
                   << " execution because someone forced our resignation at"
                   << " the Central RTI Component (CRC) level!"
                   << THLA_ENDL;
            send_hs( stderr, (char *)errmsg.str().c_str() );
            exec_terminate( __FILE__, (char *)errmsg.str().c_str() );
         }
      }
   }

   // Execute a blocking loop until the RTI responds with information for all
   // running federates
   wait_count = 0;
   while ( joined_federate_names.size() < (unsigned int)running_feds_count ) {

      // Check for shutdown.
      this->check_for_shutdown_with_termination();

      usleep( sleep_micros );

      if ( ( joined_federate_names.size() < (unsigned int)running_feds_count ) && ( ( ++wait_count % wait_check ) == 0 ) ) {
         wait_count = 0;
         if ( !is_execution_member() ) {
            ostringstream errmsg;
            errmsg << "Federate::load_and_print_running_federate_names():" << __LINE__
                   << " Unexpectedly the Federate is no longer an execution member."
                   << " This means we are either not connected to the"
                   << " RTI or we are no longer joined to the federation"
                   << " execution because someone forced our resignation at"
                   << " the Central RTI Component (CRC) level!"
                   << THLA_ENDL;
            send_hs( stderr, (char *)errmsg.str().c_str() );
            exec_terminate( __FILE__, (char *)errmsg.str().c_str() );
         }
      }
   }

   // Now, copy the new information into my data stores and restore the saved
   // information back to what is was before this routine ran (so we can get a
   // valid checkpoint).
   clear_running_feds();
   update_running_feds();

   // Print out a list of the Running Federates.
   if ( should_print( DEBUG_LEVEL_2_TRACE, DEBUG_SOURCE_FEDERATE ) ) {
      // Build the federate summary as an output string stream.
      ostringstream summary;
      unsigned int  cnt = 0;

      summary << "Federate::load_and_print_running_federate_names():"
              << __LINE__ << "\n'running_feds' data structure contains these "
              << running_feds_count << " federates:";

      // Summarize the required federates first.
      for ( unsigned int i = 0; i < (unsigned int)running_feds_count; i++ ) {
         cnt++;
         summary << "\n    " << cnt
                 << ": Found running federate '"
                 << running_feds[i].name << "'";
      }
      summary << THLA_ENDL;

      // Display the federate summary.
      send_hs( stdout, (char *)summary.str().c_str() );
   }

   // clear the entry since it was absorbed into running_feds...
   joined_federate_name_map.clear();

   // Do not un-subscribe to this MOM data; we DO want updates as federates
   // join / resign the federation!

   if ( should_print( DEBUG_LEVEL_3_TRACE, DEBUG_SOURCE_FEDERATE ) ) {
      send_hs( stdout, "Federate::load_and_print_running_federate_names():%d Done.%c",
               __LINE__, THLA_NEWLINE );
   }
}

void Federate::clear_running_feds()
{
   if ( running_feds != static_cast< KnownFederate * >( NULL ) ) {
      for ( int i = 0; i < running_feds_count; i++ ) {
         if ( running_feds[i].MOM_instance_name != static_cast< char * >( NULL ) ) {
            if ( TMM_is_alloced( running_feds[i].MOM_instance_name ) ) {
               TMM_delete_var_a( running_feds[i].MOM_instance_name );
            }
            running_feds[i].MOM_instance_name = static_cast< char * >( NULL );
         }
         if ( running_feds[i].name != static_cast< char * >( NULL ) ) {
            if ( TMM_is_alloced( running_feds[i].name ) ) {
               TMM_delete_var_a( running_feds[i].name );
            }
            running_feds[i].name = static_cast< char * >( NULL );
         }
      }
      TMM_delete_var_a( running_feds );
      running_feds = static_cast< KnownFederate * >( NULL );
   }
}

void Federate::update_running_feds()
{
   // Make a copy of the updated known feds before restoring the saved copy...
   running_feds = reinterpret_cast< KnownFederate * >(
      alloc_type( running_feds_count, "TrickHLA::KnownFederate" ) );

   if ( running_feds == static_cast< KnownFederate * >( NULL ) ) {
      send_hs( stderr, "Federate::update_running_feds():%d Could not allocate memory for running_feds.!%c",
               __LINE__, THLA_NEWLINE );
      exec_terminate( __FILE__, "Federate::update_running_feds(): Could not allocate memory for running_feds.!" );
      return;
   }

   if ( (int)joined_federate_name_map.size() != running_feds_count ) {
      // print out the contents of 'joined_federate_name_map'
      TrickHLAObjInstanceNameMap::const_iterator map_iter;
      for ( map_iter = joined_federate_name_map.begin();
            map_iter != joined_federate_name_map.end();
            ++map_iter ) {
         send_hs( stdout, "Federate::update_running_feds():%d joined_federate_name_map[%ls]=%ls %c",
                  __LINE__, mom_HLAfederate_inst_name_map[map_iter->first].c_str(),
                  map_iter->second.c_str(), THLA_NEWLINE );
      }

      for ( int i = 0; i < running_feds_count; ++i ) {
         send_hs( stdout, "Federate::update_running_feds():%d running_feds[%d]=%s %c",
                  __LINE__, i, running_feds[i].name, THLA_NEWLINE );
      }

      // terminate the execution since the counters got out of sync...
      ostringstream errmsg;
      errmsg << "Federate::update_running_feds():" << __LINE__
             << " FATAL_ERROR: joined_federate_name_map contains "
             << joined_federate_name_map.size()
             << " entries but running_feds_count = " << running_feds_count
             << "!!!" << THLA_ENDL;
      send_hs( stderr, (char *)errmsg.str().c_str() );
      exec_terminate( __FILE__, (char *)errmsg.str().c_str() );
      return;
   }

   // loop through joined_federate_name_map to build the running_feds list
   int                                        count = 0;
   TrickHLAObjInstanceNameMap::const_iterator map_iter;
   for ( map_iter = joined_federate_name_map.begin();
         map_iter != joined_federate_name_map.end(); ++map_iter ) {

      running_feds[count].name = StringUtilities::ip_strdup_wstring(
         map_iter->second.c_str() );

      running_feds[count].MOM_instance_name = StringUtilities::ip_strdup_wstring(
         mom_HLAfederate_inst_name_map[map_iter->first].c_str() );

      // If the federate was running at the time of the checkpoint, it must be
      // a 'required' federate in the restore, regardless if it is was required
      // when the federation originally started up.
      running_feds[count].required = 1;

      count++;
   }
}

void Federate::add_a_single_entry_into_running_feds()
{
   // Allocate a new structure to absorb the original values plus the new one.
   KnownFederate *temp_feds;
   temp_feds = reinterpret_cast< KnownFederate * >(
      alloc_type( running_feds_count + 1, "TrickHLA::KnownFederate" ) );

   if ( temp_feds == static_cast< KnownFederate * >( NULL ) ) {
      send_hs( stderr, "Federate::add_a_single_entry_into_running_feds():%d Could \
not allocate memory for temp_feds when attempting to add an entry into running_feds!%c",
               __LINE__, THLA_NEWLINE );
      exec_terminate( __FILE__, "Federate::add_a_single_entry_into_running_feds(): Could \
not allocate memory for temp_feds when attempting to add an entry into running_feds!" );
   } else {

      // copy current running_feds entries into tempororary structure...
      for ( int i = 0; i < running_feds_count; i++ ) {
         temp_feds[i].MOM_instance_name = TMM_strdup( running_feds[i].MOM_instance_name );
         temp_feds[i].name              = TMM_strdup( running_feds[i].name );
         temp_feds[i].required          = running_feds[i].required;
      }

      TrickHLAObjInstanceNameMap::const_iterator map_iter;
      map_iter                                        = joined_federate_name_map.begin();
      temp_feds[running_feds_count].MOM_instance_name = StringUtilities::ip_strdup_wstring( mom_HLAfederate_inst_name_map[map_iter->first].c_str() );
      temp_feds[running_feds_count].name              = StringUtilities::ip_strdup_wstring( map_iter->second.c_str() );
      temp_feds[running_feds_count].required          = 1;

      // delete running_feds data structure.
      clear_running_feds();

      // assign temp_feds into running_feds
      running_feds = temp_feds;

      ++running_feds_count; // make the new running_feds_count size permanent
   }

#if 0
   if ( should_print( DEBUG_LEVEL_2_TRACE, DEBUG_SOURCE_FEDERATE ) ) {
      send_hs( stdout, "Federate::add_a_single_entry_into_running_feds():%d Exiting routine, here is what running_feds contains:%c",
               __LINE__, THLA_NEWLINE);
      for ( int t = 0; t < running_feds_count; t++) {
         send_hs( stdout, "Federate::add_a_single_entry_into_running_feds():%d running_feds[%d].MOM_instance_name='%s'%c",
                  __LINE__, t, running_feds[t].MOM_instance_name, THLA_NEWLINE);
         send_hs( stdout, "Federate::add_a_single_entry_into_running_feds():%d running_feds[%d].name='%s'%c",
                  __LINE__, t, running_feds[t].name, THLA_NEWLINE);
         send_hs( stdout, "Federate::add_a_single_entry_into_running_feds():%d running_feds[%d].required=%d %c",
                  __LINE__, t, running_feds[t].required, THLA_NEWLINE);
      }
   }
#endif
}

void Federate::add_MOM_HLAfederate_instance_id(
   ObjectInstanceHandle instance_hndl,
   wstring const &      instance_name )
{
   mom_HLAfederate_inst_name_map[instance_hndl] = instance_name;
}

void Federate::remove_MOM_HLAfederate_instance_id(
   ObjectInstanceHandle instance_hndl )
{
   remove_federate_instance_id( instance_hndl );
   remove_MOM_HLAfederation_instance_id( instance_hndl );

   char *                               tMOMName  = NULL;
   char *                               tFedName  = NULL;
   bool                                 foundName = false;
   TrickHLAObjInstanceNameMap::iterator iter;

   iter = mom_HLAfederate_inst_name_map.find( instance_hndl );
   if ( iter != mom_HLAfederate_inst_name_map.end() ) {
      tMOMName  = StringUtilities::ip_strdup_wstring( iter->second.c_str() );
      foundName = true;
      mom_HLAfederate_inst_name_map.erase( iter );
   }

   // if the federate_id was not found, there is nothing else to do so exit the routine...
   if ( !foundName ) {
      return;
   }

   // search for the federate information from running_feds...
   foundName = false;
   for ( int i = 0; i < running_feds_count; i++ ) {
      if ( !strcmp( running_feds[i].MOM_instance_name, tMOMName ) ) {
         foundName = true;
         tFedName  = TMM_strdup( running_feds[i].name );
      }
   }

   // if the name was not found, there is nothing else to do so exit the routine...
   if ( !foundName ) {
      return;
   }

   // otherwise, the name was found. it needs to be deleted from the list of running_feds.
   // since the memory is Trick-controlled and not random access, the only way to delete
   // it is to copy the whole element list omitting the requested name...
   KnownFederate *tmp_feds;

   // allocate temporary list...
   tmp_feds = reinterpret_cast< KnownFederate * >(
      alloc_type( running_feds_count - 1, "TrickHLA::KnownFederate" ) );
   if ( tmp_feds == static_cast< KnownFederate * >( NULL ) ) {
      send_hs( stderr, "Federate::remove_discovered_object_federate_instance_id():%d Could not allocate memory for tmp_feds.!%c",
               __LINE__, THLA_NEWLINE );
      exec_terminate( __FILE__, "Federate::remove_discovered_object_federate_instance_id(): Could not allocate memory for tmp_feds.!" );
      return;
   }
   // now, copy everything minus the requested name from the original list...
   int tmp_feds_cnt = 0;
   for ( int i = 0; i < running_feds_count; i++ ) {
      // if the name is not the one we are looking for...
      if ( strcmp( running_feds[i].name, tFedName ) ) {
         if ( running_feds[i].MOM_instance_name != NULL ) {
            tmp_feds[tmp_feds_cnt].MOM_instance_name = TMM_strdup( running_feds[i].MOM_instance_name );
         }
         tmp_feds[tmp_feds_cnt].name     = TMM_strdup( running_feds[i].name );
         tmp_feds[tmp_feds_cnt].required = running_feds[i].required;
         tmp_feds_cnt++;
      }
   }

   // now, clear out the original memory...
   clear_running_feds();

   // assign the new element count into running_feds_count.
   running_feds_count = tmp_feds_cnt;

   // assign pointer from the temporary list to the permanent list...
   running_feds = tmp_feds;

   if ( should_print( DEBUG_LEVEL_2_TRACE, DEBUG_SOURCE_FEDERATE ) ) {
      string id_str;
      StringUtilities::to_string( id_str, instance_hndl );
      send_hs( stderr, "Federate::remove_discovered_object_federate_instance_id():%d \
Removed Federate '%s' Instance-ID:%s Valid-ID:%s %c",
               __LINE__, tFedName, id_str.c_str(),
               ( instance_hndl.isValid() ? "Yes" : "No" ), THLA_NEWLINE );
   }
}

void Federate::add_MOM_HLAfederation_instance_id(
   ObjectInstanceHandle instance_hndl )
{
   string id_str;
   StringUtilities::to_string( id_str, instance_hndl );
   wstring id_ws;
   StringUtilities::to_wstring( id_ws, id_str );
   mom_HLAfederation_instance_name_map[instance_hndl] = id_ws;
}

void Federate::remove_MOM_HLAfederation_instance_id(
   ObjectInstanceHandle instance_hndl )
{
   TrickHLAObjInstanceNameMap::iterator iter;
   iter = mom_HLAfederation_instance_name_map.find( instance_hndl );

   if ( iter != mom_HLAfederation_instance_name_map.end() ) {
      mom_HLAfederation_instance_name_map.erase( iter );
   }
}

void Federate::write_running_feds_file(
   char *file_name ) throw( const char * )
{
   char     full_path[1024];
   ofstream file;

   snprintf( full_path, sizeof( full_path ), "%s/%s.running_feds", this->HLA_save_directory, file_name );
   file.open( full_path, ios::out );
   if ( file.is_open() ) {
      file << running_feds_count << endl;

      // echo the contents of running_feds into file...
      for ( int i = 0; i < running_feds_count; i++ ) {
         file << TMM_strdup( running_feds[i].MOM_instance_name ) << endl;
         file << TMM_strdup( running_feds[i].name ) << endl;
         file << running_feds[i].required << endl;
      }

      file.close(); // close the file.

   } else {
      ostringstream msg;
      msg << "Federate::write_running_feds_file():" << __LINE__
          << " Failed to open file '" << full_path << "' for writing!" << THLA_ENDL;
      send_hs( stderr, (char *)msg.str().c_str() );
      exec_terminate( __FILE__, (char *)msg.str().c_str() );
   }
}

/*!
 *  @job_class{freeze}
 */
void Federate::request_federation_save()
{
   // Just return if HLA save and restore is not supported by the simulation
   // initialization scheme selected by the user.
   if ( !is_HLA_save_and_restore_supported() ) {
      return;
   }

   // Macro to save the FPU Control Word register value.
   TRICKHLA_SAVE_FPU_CONTROL_WORD;

   try {
      if ( should_print( DEBUG_LEVEL_2_TRACE, DEBUG_SOURCE_FEDERATE ) ) {
         send_hs( stdout, "Federate::request_federation_save():%d save_name:%ls %c",
                  __LINE__, save_name.c_str(), THLA_NEWLINE );
      }
      RTI_ambassador->requestFederationSave( save_name );
   } catch ( FederateNotExecutionMember &e ) {
      send_hs( stderr, "Federate::request_federation_save():%d EXCEPTION: FederateNotExecutionMember %c",
               __LINE__, THLA_NEWLINE );
   } catch ( SaveInProgress &e ) {
      send_hs( stderr, "Federate::request_federation_save():%d EXCEPTION: SaveInProgress %c",
               __LINE__, THLA_NEWLINE );
   } catch ( RestoreInProgress &e ) {
      send_hs( stderr, "Federate::request_federation_save():%d EXCEPTION: RestoreInProgress %c",
               __LINE__, THLA_NEWLINE );
   } catch ( NotConnected &e ) {
      send_hs( stderr, "Federate::request_federation_save():%d EXCEPTION: NotConnected %c",
               __LINE__, THLA_NEWLINE );
   } catch ( RTIinternalError &e ) {
      string rti_err_msg;
      StringUtilities::to_string( rti_err_msg, e.what() );

      send_hs( stderr, "Federate::request_federation_save():%d EXCEPTION: RTIinternalError: '%s'%c",
               __LINE__, rti_err_msg.c_str(), THLA_NEWLINE );
   }

   // Macro to restore the saved FPU Control Word register value.
   TRICKHLA_RESTORE_FPU_CONTROL_WORD;
   TRICKHLA_VALIDATE_FPU_CONTROL_WORD;
}

void Federate::restore_checkpoint(
   char *file_name )
{
   string trick_filename = string( file_name );
   // DANNY2.7 prepend federation name to the filename (if it's not already prepended)
   size_t federation_len = strlen( get_federation_name() );
   if ( trick_filename.compare( 0, federation_len, get_federation_name() ) != 0 ) {
      trick_filename = string( get_federation_name() ) + "_" + string( file_name );
   }
   send_hs( stdout, "Federate::restore_checkpoint() Restoring checkpoint file %s%c",
            trick_filename.c_str(), THLA_NEWLINE );

   // DANNY2.7 must init all data recording groups since we are restarting at init
   // time before Trick would normally do this. Prior to Trick 10.8, the only way
   // to do this is by calling each recording group init() routine in the S_define

   // This will run pre-load-checkpoint jobs, clear memory, read checkpoint
   // file, and run restart jobs.
   load_checkpoint( ( string( this->HLA_save_directory ) + "/" + trick_filename ).c_str() );

   load_checkpoint_job();

   // If exec_set_freeze_command(true) is in master fed's input file when
   // check-pointed, then restore starts up in freeze.
   // DANNY2.7 Clear non-master fed's freeze command so it doesnt cause
   // unnecessary freeze interaction to be sent.
   if ( !execution_control->is_master() ) {
      exec_set_freeze_command( false );
   }

   send_hs( stdout, "Federate::restore_checkpoint():%d Checkpoint file load complete.%c",
            __LINE__, THLA_NEWLINE );

   // indicate that the restore was completed successfully
   this->restore_process = Restore_Complete;

   // make a copy of the 'restore_process' ENUM just in case it gets overwritten.
   this->prev_restore_process = this->restore_process;
}

void Federate::inform_RTI_of_restore_completion()
{
   // Macro to save the FPU Control Word register value.
   TRICKHLA_SAVE_FPU_CONTROL_WORD;

   if ( this->prev_restore_process == Restore_Complete ) {

      if ( should_print( DEBUG_LEVEL_2_TRACE, DEBUG_SOURCE_FEDERATE ) ) {
         send_hs( stdout, "Federate::inform_RTI_of_restore_completion():%d Restore Complete.%c",
                  __LINE__, THLA_NEWLINE );
      }

      try {
         RTI_ambassador->federateRestoreComplete();
      } catch ( RestoreNotRequested &e ) {
         send_hs( stderr, "Federate::inform_RTI_of_restore_completion():%d -- restore complete -- EXCEPTION: RestoreNotRequested %c",
                  __LINE__, THLA_NEWLINE );
      } catch ( FederateNotExecutionMember &e ) {
         send_hs( stderr, "Federate::inform_RTI_of_restore_completion():%d -- restore complete -- EXCEPTION: FederateNotExecutionMember %c",
                  __LINE__, THLA_NEWLINE );
      } catch ( SaveInProgress &e ) {
         send_hs( stderr, "Federate::inform_RTI_of_restore_completion():%d -- restore complete -- EXCEPTION: SaveInProgress %c",
                  __LINE__, THLA_NEWLINE );
      } catch ( NotConnected &e ) {
         send_hs( stderr, "Federate::inform_RTI_of_restore_completion():%d -- restore complete -- EXCEPTION: NotConnected %c",
                  __LINE__, THLA_NEWLINE );
      } catch ( RTIinternalError &e ) {
         string rti_err_msg;
         StringUtilities::to_string( rti_err_msg, e.what() );

         send_hs( stderr, "Federate::inform_RTI_of_restore_completion():%d -- restore complete -- EXCEPTION: RTIinternalError: '%s'%c",
                  __LINE__, rti_err_msg.c_str(), THLA_NEWLINE );
      }

   } else if ( this->prev_restore_process == Restore_Failed ) {

      if ( should_print( DEBUG_LEVEL_2_TRACE, DEBUG_SOURCE_FEDERATE ) ) {
         send_hs( stdout, "Federate::inform_RTI_of_restore_completion():%d Restore Failed!%c",
                  __LINE__, THLA_NEWLINE );
      }

      try {
         RTI_ambassador->federateRestoreNotComplete();
      } catch ( RestoreNotRequested &e ) {
         send_hs( stderr, "Federate::inform_RTI_of_restore_completion():%d -- restore NOT complete -- EXCEPTION: RestoreNotRequested %c",
                  __LINE__, THLA_NEWLINE );
      } catch ( FederateNotExecutionMember &e ) {
         send_hs( stderr, "Federate::inform_RTI_of_restore_completion():%d -- restore NOT complete -- EXCEPTION: FederateNotExecutionMember %c",
                  __LINE__, THLA_NEWLINE );
      } catch ( SaveInProgress &e ) {
         send_hs( stderr, "Federate::inform_RTI_of_restore_completion():%d -- restore NOT complete -- EXCEPTION: SaveInProgress %c",
                  __LINE__, THLA_NEWLINE );
      } catch ( NotConnected &e ) {
         send_hs( stderr, "Federate::inform_RTI_of_restore_completion():%d -- restore NOT complete -- EXCEPTION: NotConnected %c",
                  __LINE__, THLA_NEWLINE );
      } catch ( RTIinternalError &e ) {
         string rti_err_msg;
         StringUtilities::to_string( rti_err_msg, e.what() );

         send_hs( stderr, "Federate::inform_RTI_of_restore_completion():%d -- restore NOT complete -- EXCEPTION: RTIinternalError: '%s'%c",
                  __LINE__, rti_err_msg.c_str(), THLA_NEWLINE );
      }
   } else {
      send_hs( stdout, "Federate::inform_RTI_of_restore_completion():%d ERROR: \
Unexpected restore process %d, which is not 'Restore_Complete' or 'Restore_Request_Failed'.%c",
               __LINE__, restore_process, THLA_NEWLINE );
   }

   // Macro to restore the saved FPU Control Word register value.
   TRICKHLA_RESTORE_FPU_CONTROL_WORD;
   TRICKHLA_VALIDATE_FPU_CONTROL_WORD;
}

void Federate::read_running_feds_file(
   char *file_name ) throw( const char * )
{
   char     full_path[1024];
   ifstream file;

   // DANNY2.7 prepend federation name to the filename (if it's not already prepended)
   if ( strncmp( file_name, get_federation_name(), strlen( get_federation_name() ) ) == 0 ) {
      // already prepended
      snprintf( full_path, sizeof( full_path ), "%s/%s.running_feds", this->HLA_save_directory, file_name );
   } else {
      // prepend it here
      snprintf( full_path, sizeof( full_path ), "%s/%s_%s.running_feds", this->HLA_save_directory, get_federation_name(), file_name );
   }

   file.open( full_path, ios::in );
   if ( file.is_open() ) {

      // clear out the known_feds from memory...
      for ( int i = 0; i < known_feds_count; i++ ) {
         if ( known_feds[i].MOM_instance_name != static_cast< char * >( NULL ) ) {
            if ( TMM_is_alloced( known_feds[i].MOM_instance_name ) ) {
               TMM_delete_var_a( known_feds[i].MOM_instance_name );
            }
            known_feds[i].MOM_instance_name = static_cast< char * >( NULL );
         }
         if ( known_feds[i].name != static_cast< char * >( NULL ) ) {
            if ( TMM_is_alloced( known_feds[i].name ) ) {
               TMM_delete_var_a( known_feds[i].name );
            }
            known_feds[i].name = static_cast< char * >( NULL );
         }
      }
      TMM_delete_var_a( known_feds );
      known_feds = NULL;

      file >> known_feds_count;

      // re-allocate it...
      known_feds = reinterpret_cast< KnownFederate * >(
         alloc_type( known_feds_count, "TrickHLA::KnownFederate" ) );
      if ( known_feds == static_cast< KnownFederate * >( NULL ) ) {
         send_hs( stdout, "Federate::read_running_feds_file():%d Could not allocate memory for known_feds.! %c",
                  __LINE__, THLA_NEWLINE );
         exec_terminate( __FILE__, "Federate::read_running_feds_file() Could not allocate memory for known_feds.!" );
      }

      string current_line;
      for ( int i = 0; i < known_feds_count; i++ ) {
         file >> current_line;
         known_feds[i].MOM_instance_name = TMM_strdup( (char *)current_line.c_str() );

         file >> current_line;
         known_feds[i].name = TMM_strdup( (char *)current_line.c_str() );

         file >> current_line;
         known_feds[i].required = atoi( current_line.c_str() );
      }

      file.close(); // close the file before exitting
   } else {
      ostringstream msg;
      msg << "Federate::read_running_feds_file()" << __LINE__
          << " Failed to open file '" << full_path << "'!" << THLA_ENDL;
      send_hs( stderr, (char *)msg.str().c_str() );
      exec_terminate( __FILE__, (char *)msg.str().c_str() );
   }
}

void Federate::copy_running_feds_into_known_feds()
{
   // clear out the known_feds from memory...
   for ( int i = 0; i < known_feds_count; i++ ) {
      if ( known_feds[i].MOM_instance_name != static_cast< char * >( NULL ) ) {

         if ( TMM_is_alloced( known_feds[i].MOM_instance_name ) ) {
            TMM_delete_var_a( known_feds[i].MOM_instance_name );
         }
         known_feds[i].MOM_instance_name = static_cast< char * >( NULL );
      }
      if ( known_feds[i].name != static_cast< char * >( NULL ) ) {
         if ( TMM_is_alloced( known_feds[i].name ) ) {
            TMM_delete_var_a( known_feds[i].name );
         }
         known_feds[i].name = static_cast< char * >( NULL );
      }
   }
   TMM_delete_var_a( known_feds );

   // re-allocate it...
   known_feds = reinterpret_cast< KnownFederate * >(
      alloc_type( running_feds_count, "TrickHLA::KnownFederate" ) );
   if ( known_feds == static_cast< KnownFederate * >( NULL ) ) {
      send_hs( stdout, "Federate::copy_running_feds_into_known_feds():%d Could not allocate memory for known_feds.! %c",
               __LINE__, THLA_NEWLINE );
      exec_terminate( __FILE__, "Federate::copy_running_feds_into_known_feds(): Could not allocate memory for known_feds.!" );
      return;
   }

   // now, copy everything from running_feds into known_feds...
   known_feds_count = 0;
   for ( int i = 0; i < running_feds_count; i++ ) {
      known_feds[known_feds_count].MOM_instance_name = TMM_strdup( running_feds[i].MOM_instance_name );
      known_feds[known_feds_count].name              = TMM_strdup( running_feds[i].name );
      known_feds[known_feds_count].required          = running_feds[i].required;
      known_feds_count++;
   }
}

/*!
 * \par<b>Assumptions and Limitations:</b>
 * - Currently only used Used with IMSIM initialization scheme; only for restore at simulation startup.
 *  @job_class{environment}
 */
void Federate::restart_checkpoint()
{
   if ( should_print( DEBUG_LEVEL_3_TRACE, DEBUG_SOURCE_FEDERATE ) ) {
      send_hs( stdout, "Federate::restart_checkpoint():%d%c",
               __LINE__, THLA_NEWLINE );
   }

   // Macro to save the FPU Control Word register value.
   TRICKHLA_SAVE_FPU_CONTROL_WORD;

   try {
      HLAinteger64Time fedTime;
      RTI_ambassador->queryLogicalTime( fedTime );
      set_granted_time( fedTime );
   } catch ( FederateNotExecutionMember &e ) {
      send_hs( stderr, "Federate::restart_checkpoint():%d queryLogicalTime EXCEPTION: FederateNotExecutionMember %c",
               __LINE__, THLA_NEWLINE );
   } catch ( SaveInProgress &e ) {
      send_hs( stderr, "Federate::restart_checkpoint():%d queryLogicalTime EXCEPTION: SaveInProgress %c",
               __LINE__, THLA_NEWLINE );
   } catch ( RestoreInProgress &e ) {
      send_hs( stderr, "Federate::restart_checkpoint():%d queryLogicalTime EXCEPTION: RestoreInProgress %c",
               __LINE__, THLA_NEWLINE );
   } catch ( NotConnected &e ) {
      send_hs( stderr, "Federate::restart_checkpoint():%d queryLogicalTime EXCEPTION: NotConnected %c",
               __LINE__, THLA_NEWLINE );
   } catch ( RTIinternalError &e ) {
      send_hs( stderr, "Federate::restart_checkpoint():%d queryLogicalTime EXCEPTION: RTIinternalError %c",
               __LINE__, THLA_NEWLINE );
   }

   // Macro to restore the saved FPU Control Word register value.
   TRICKHLA_RESTORE_FPU_CONTROL_WORD;
   TRICKHLA_VALIDATE_FPU_CONTROL_WORD;

   this->HLA_time        = get_granted_time();
   requested_time        = granted_time;
   this->restore_process = No_Restore;

   reinstate_logged_sync_pts();

   federation_restored();
}

/*!
 *  @job_class{freeze}
 */
void Federate::federation_saved()
{
   if ( should_print( DEBUG_LEVEL_3_TRACE, DEBUG_SOURCE_FEDERATE ) ) {
      send_hs( stdout, "Federate::federation_saved():%d%c",
               __LINE__, THLA_NEWLINE );
   }
   announce_save         = false;
   save_label_generated  = false;
   save_request_complete = false;
   cstr_save_label[0]    = '\0';
   str_save_label        = "";
   ws_save_label         = L"";
   ;
   save_name               = L"";
   checkpoint_file_name[0] = '\0';

   if ( unfreeze_after_save ) {
      announce_freeze = false; // this keeps from generating the RUNFED_v2 sync point since it's not needed
      // exit freeze mode.
      un_freeze();
   }
}

/*!
 *  @job_class{freeze}
 */
void Federate::federation_restored()
{
   if ( should_print( DEBUG_LEVEL_3_TRACE, DEBUG_SOURCE_FEDERATE ) ) {
      send_hs( stdout, "Federate::federation_restored():%d%c",
               __LINE__, THLA_NEWLINE );
   }
   complete_restore();
   start_to_restore      = false;
   announce_restore      = false;
   save_label_generated  = false;
   restore_begun         = false;
   restore_is_imminent   = false;
   cstr_restore_label[0] = '\0';
   str_restore_label     = "";
   ws_restore_label.assign( str_restore_label.begin(), str_restore_label.end() );
   this->restore_process = No_Restore;
}

void Federate::wait_for_federation_restore_begun()
{
   if ( should_print( DEBUG_LEVEL_3_TRACE, DEBUG_SOURCE_FEDERATE ) ) {
      send_hs( stdout, "Federate::wait_for_federation_restore_begun():%d Waiting...%c",
               __LINE__, THLA_NEWLINE );
   }
   unsigned int sleep_micros = 1000;
   unsigned int wait_count   = 0;
   unsigned int wait_check   = 10000000 / sleep_micros; // Number of wait cycles for 10 seconds

   while ( !this->restore_begun ) {

      // Check for shutdown.
      this->check_for_shutdown_with_termination();

      usleep( sleep_micros ); // sleep until RTI responds...

      if ( ( !this->restore_begun ) && ( ( ++wait_count % wait_check ) == 0 ) ) {
         wait_count = 0;
         if ( !is_execution_member() ) {
            ostringstream errmsg;
            errmsg << "Federate::wait_for_federation_restore_begun():" << __LINE__
                   << " Unexpectedly the Federate is no longer an execution member."
                   << " This means we are either not connected to the"
                   << " RTI or we are no longer joined to the federation"
                   << " execution because someone forced our resignation at"
                   << " the Central RTI Component (CRC) level!"
                   << THLA_ENDL;
            send_hs( stderr, (char *)errmsg.str().c_str() );
            exec_terminate( __FILE__, (char *)errmsg.str().c_str() );
         }
      }
   }
   if ( should_print( DEBUG_LEVEL_3_TRACE, DEBUG_SOURCE_FEDERATE ) ) {
      send_hs( stdout, "Federate::wait_for_federation_restore_begun():%d Done.%c",
               __LINE__, THLA_NEWLINE );
   }
}

void Federate::wait_until_federation_is_ready_to_restore()
{
   if ( should_print( DEBUG_LEVEL_3_TRACE, DEBUG_SOURCE_FEDERATE ) ) {
      send_hs( stdout, "Federate::wait_until_federation_is_ready_to_restore():%d Waiting...%c",
               __LINE__, THLA_NEWLINE );
   }
   unsigned int sleep_micros = 1000;
   unsigned int wait_count   = 0;
   unsigned int wait_check   = 10000000 / sleep_micros; // Number of wait cycles for 10 seconds

   while ( !this->start_to_restore ) {

      // Check for shutdown.
      this->check_for_shutdown_with_termination();

      usleep( sleep_micros ); // sleep until RTI responds...

      if ( ( !this->start_to_restore ) && ( ( ++wait_count % wait_check ) == 0 ) ) {
         wait_count = 0;
         if ( !is_execution_member() ) {
            ostringstream errmsg;
            errmsg << "Federate::wait_until_federation_is_ready_to_restore():" << __LINE__
                   << " Unexpectedly the Federate is no longer an execution member."
                   << " This means we are either not connected to the"
                   << " RTI or we are no longer joined to the federation"
                   << " execution because someone forced our resignation at"
                   << " the Central RTI Component (CRC) level!"
                   << THLA_ENDL;
            send_hs( stderr, (char *)errmsg.str().c_str() );
            exec_terminate( __FILE__, (char *)errmsg.str().c_str() );
         }
      }
   }
   if ( should_print( DEBUG_LEVEL_3_TRACE, DEBUG_SOURCE_FEDERATE ) ) {
      send_hs( stdout, "Federate::wait_until_federation_is_ready_to_restore():%d Done.%c",
               __LINE__, THLA_NEWLINE );
   }
}

string Federate::wait_for_federation_restore_to_complete()
{
   string tRetString;

   if ( should_print( DEBUG_LEVEL_3_TRACE, DEBUG_SOURCE_FEDERATE ) ) {
      send_hs( stdout, "Federate::wait_for_federation_restore_to_complete():%d Waiting...%c",
               __LINE__, THLA_NEWLINE );
   }

   if ( this->restore_failed ) {
      tRetString = "Federate::wait_for_federation_restore_to_complete() "
                   "Restore of federate failed\nTERMINATING SIMULATION!";
      return tRetString;
   }

   if ( this->federation_restore_failed_callback_complete ) {
      tRetString = "Federate::wait_for_federation_restore_to_complete() "
                   "Federation restore failed\nTERMINATING SIMULATION!";
      return tRetString;
   }

   if ( restore_process == Restore_Failed ) {
      // before we enter the blocking loop, the RTI informed us that it accepted
      // the failure of the the federate restore. build and return a message.
      tRetString = "Federate::wait_for_federation_restore_to_complete() "
                   "Federation restore FAILED! Look at the message from the "
                   "Federate::print_restore_failure_reason() routine "
                   "for a reason why the federation restore failed.\n"
                   "TERMINATING SIMULATION!";
      return tRetString;
   }

   unsigned int sleep_micros = 1000;
   unsigned int wait_count   = 0;
   unsigned int wait_check   = 10000000 / sleep_micros; // Number of wait cycles for 10 seconds

   // nobody reported any problems, wait until the restore is completed.
   while ( !this->restore_completed ) {

      // Check for shutdown.
      this->check_for_shutdown_with_termination();

      if ( running_feds_count_at_time_of_restore > running_feds_count ) {
         // someone has resigned since the federation restore has been initiated.
         // build a message detailing what happened and exit the routine.
         tRetString = "Federate::wait_for_federation_restore_to_complete() "
                      "While waiting for restore of the federation "
                      "a federate resigned before the federation restore "
                      "completed!\nTERMINATING SIMULATION!";
         return tRetString;
      } else {
         usleep( sleep_micros ); // sleep until RTI responds...

         if ( ( !this->restore_completed ) && ( ( ++wait_count % wait_check ) == 0 ) ) {
            wait_count = 0;
            if ( !is_execution_member() ) {
               ostringstream errmsg;
               errmsg << "Federate::wait_for_federation_restore_to_complete():" << __LINE__
                      << " Unexpectedly the Federate is no longer an execution member."
                      << " This means we are either not connected to the"
                      << " RTI or we are no longer joined to the federation"
                      << " execution because someone forced our resignation at"
                      << " the Central RTI Component (CRC) level!"
                      << THLA_ENDL;
               send_hs( stderr, (char *)errmsg.str().c_str() );
               exec_terminate( __FILE__, (char *)errmsg.str().c_str() );
            }
         }
      }
   }

   if ( restore_process == Restore_Failed ) {
      // after this federate restore blocking loop has finished, check if the RTI
      // accepted the failure of the federate restore. build and return a message.
      tRetString = "Federate::wait_for_federation_restore_to_complete() "
                   "Federation restore FAILED! Look at the message from the "
                   "Federate::print_restore_failure_reason() routine "
                   "for a reason why the federation restore failed.\n"
                   "TERMINATING SIMULATION!";
      return tRetString;
   }

   if ( should_print( DEBUG_LEVEL_3_TRACE, DEBUG_SOURCE_FEDERATE ) ) {
      send_hs( stdout, "Federate::wait_for_federation_restore_to_complete():%d Done.%c",
               __LINE__, THLA_NEWLINE );
   }
   return tRetString;
}

void Federate::wait_for_restore_request_callback()
{
   if ( should_print( DEBUG_LEVEL_3_TRACE, DEBUG_SOURCE_FEDERATE ) ) {
      send_hs( stdout, "Federate::wait_for_restore_request_callback():%d Waiting...%c",
               __LINE__, THLA_NEWLINE );
   }
   unsigned int sleep_micros = 1000;
   unsigned int wait_count   = 0;
   unsigned int wait_check   = 10000000 / sleep_micros; // Number of wait cycles for 10 seconds

   while ( !has_restore_process_restore_request_failed() && !has_restore_process_restore_request_succeeded() ) {

      // Check for shutdown.
      this->check_for_shutdown_with_termination();

      usleep( sleep_micros ); // sleep until RTI responds...

      if ( ( !has_restore_process_restore_request_failed() && !has_restore_process_restore_request_succeeded() ) && ( ( ++wait_count % wait_check ) == 0 ) ) {
         wait_count = 0;
         if ( !is_execution_member() ) {
            ostringstream errmsg;
            errmsg << "Federate::wait_for_restore_request_callback():" << __LINE__
                   << " Unexpectedly the Federate is no longer an execution member."
                   << " This means we are either not connected to the"
                   << " RTI or we are no longer joined to the federation"
                   << " execution because someone forced our resignation at"
                   << " the Central RTI Component (CRC) level!"
                   << THLA_ENDL;
            send_hs( stderr, (char *)errmsg.str().c_str() );
            exec_terminate( __FILE__, (char *)errmsg.str().c_str() );
         }
      }
   }
   if ( should_print( DEBUG_LEVEL_3_TRACE, DEBUG_SOURCE_FEDERATE ) ) {
      send_hs( stdout, "Federate::wait_for_restore_request_callback():%d Done.%c",
               __LINE__, THLA_NEWLINE );
   }
}

void Federate::wait_for_restore_status_to_complete()
{
   if ( should_print( DEBUG_LEVEL_3_TRACE, DEBUG_SOURCE_FEDERATE ) ) {
      send_hs( stdout, "Federate::wait_for_restore_status_to_complete():%d Waiting...%c",
               __LINE__, THLA_NEWLINE );
   }
   unsigned int sleep_micros = 1000;
   unsigned int wait_count   = 0;
   unsigned int wait_check   = 10000000 / sleep_micros; // Number of wait cycles for 10 seconds

   while ( !restore_request_complete ) {

      // Check for shutdown.
      this->check_for_shutdown_with_termination();

      usleep( sleep_micros ); // sleep until RTI responds...

      if ( ( !restore_request_complete ) && ( ( ++wait_count % wait_check ) == 0 ) ) {
         wait_count = 0;
         if ( !is_execution_member() ) {
            ostringstream errmsg;
            errmsg << "Federate::wait_for_restore_status_to_complete():" << __LINE__
                   << " Unexpectedly the Federate is no longer an execution member."
                   << " This means we are either not connected to the"
                   << " RTI or we are no longer joined to the federation"
                   << " execution because someone forced our resignation at"
                   << " the Central RTI Component (CRC) level!"
                   << THLA_ENDL;
            send_hs( stderr, (char *)errmsg.str().c_str() );
            exec_terminate( __FILE__, (char *)errmsg.str().c_str() );
         }
      }
   }
   if ( should_print( DEBUG_LEVEL_3_TRACE, DEBUG_SOURCE_FEDERATE ) ) {
      send_hs( stdout, "Federate::wait_for_restore_status_to_complete():%d Done.%c",
               __LINE__, THLA_NEWLINE );
   }
}

void Federate::wait_for_save_status_to_complete()
{
   if ( should_print( DEBUG_LEVEL_3_TRACE, DEBUG_SOURCE_FEDERATE ) ) {
      send_hs( stdout, "Federate::wait_for_save_status_to_complete():%d Waiting...%c",
               __LINE__, THLA_NEWLINE );
   }
   unsigned int sleep_micros = 1000;
   unsigned int wait_count   = 0;
   unsigned int wait_check   = 10000000 / sleep_micros; // Number of wait cycles for 10 seconds

   while ( !save_request_complete ) {

      // Check for shutdown.
      this->check_for_shutdown_with_termination();

      usleep( sleep_micros ); // sleep until RTI responds...

      if ( ( !save_request_complete ) && ( ( ++wait_count % wait_check ) == 0 ) ) {
         wait_count = 0;
         if ( !is_execution_member() ) {
            ostringstream errmsg;
            errmsg << "Federate::wait_for_save_status_to_complete():" << __LINE__
                   << " Unexpectedly the Federate is no longer an execution member."
                   << " This means we are either not connected to the"
                   << " RTI or we are no longer joined to the federation"
                   << " execution because someone forced our resignation at"
                   << " the Central RTI Component (CRC) level!"
                   << THLA_ENDL;
            send_hs( stderr, (char *)errmsg.str().c_str() );
            exec_terminate( __FILE__, (char *)errmsg.str().c_str() );
         }
      }
   }
   if ( should_print( DEBUG_LEVEL_3_TRACE, DEBUG_SOURCE_FEDERATE ) ) {
      send_hs( stdout, "Federate::wait_for_save_status_to_complete():%d Done.%c",
               __LINE__, THLA_NEWLINE );
   }
}

void Federate::wait_for_federation_restore_failed_callback_to_complete()
{
   if ( should_print( DEBUG_LEVEL_3_TRACE, DEBUG_SOURCE_FEDERATE ) ) {
      send_hs( stdout, "Federate::wait_for_federation_restore_failed_callback_to_complete():%d Waiting...%c",
               __LINE__, THLA_NEWLINE );
   }
   unsigned int sleep_micros = 1000;
   unsigned int wait_count   = 0;
   unsigned int wait_check   = 10000000 / sleep_micros; // Number of wait cycles for 10 seconds

   while ( !federation_restore_failed_callback_complete ) {

      // Check for shutdown.
      this->check_for_shutdown_with_termination();

      // if the federate has already been restored, do not wait for a signal
      // from the RTI that the federation restore failed, you'll never get it!
      if ( this->restore_completed ) {
         if ( should_print( DEBUG_LEVEL_3_TRACE, DEBUG_SOURCE_FEDERATE ) ) {
            send_hs( stdout, "Federate::wait_for_federation_restore_failed_callback_to_complete():%d Restore Complete, Done.%c",
                     __LINE__, THLA_NEWLINE );
         }
         return;
      }
      usleep( sleep_micros ); // sleep until RTI responds...

      if ( ( !federation_restore_failed_callback_complete ) && ( ( ++wait_count % wait_check ) == 0 ) ) {
         wait_count = 0;
         if ( !is_execution_member() ) {
            ostringstream errmsg;
            errmsg << "Federate::wait_for_federation_restore_failed_callback_to_complete():" << __LINE__
                   << " Unexpectedly the Federate is no longer an execution member."
                   << " This means we are either not connected to the"
                   << " RTI or we are no longer joined to the federation"
                   << " execution because someone forced our resignation at"
                   << " the Central RTI Component (CRC) level!"
                   << THLA_ENDL;
            send_hs( stderr, (char *)errmsg.str().c_str() );
            exec_terminate( __FILE__, (char *)errmsg.str().c_str() );
         }
      }
   }
   if ( should_print( DEBUG_LEVEL_3_TRACE, DEBUG_SOURCE_FEDERATE ) ) {
      send_hs( stdout, "Federate::wait_for_federation_restore_failed_callback_to_complete():%d Done.%c",
               __LINE__, THLA_NEWLINE );
   }
}

void Federate::request_federation_save_status()
{
   if ( should_print( DEBUG_LEVEL_3_TRACE, DEBUG_SOURCE_FEDERATE ) ) {
      send_hs( stdout, "Federate::request_federation_save_status():%d%c",
               __LINE__, THLA_NEWLINE );
   }

   // Macro to save the FPU Control Word register value.
   TRICKHLA_SAVE_FPU_CONTROL_WORD;

   try {
      RTI_ambassador->queryFederationSaveStatus();
   } catch ( FederateNotExecutionMember &e ) {
      send_hs( stderr, "Federate::request_federation_save_status():%d EXCEPTION: FederateNotExecutionMember %c",
               __LINE__, THLA_NEWLINE );
   } catch ( RestoreInProgress &e ) {
      send_hs( stderr, "Federate::request_federation_save_status():%d EXCEPTION: RestoreInProgress %c",
               __LINE__, THLA_NEWLINE );
   } catch ( NotConnected &e ) {
      send_hs( stderr, "Federate::request_federation_save_status():%d EXCEPTION: NotConnected %c",
               __LINE__, THLA_NEWLINE );
   } catch ( RTIinternalError &e ) {
      string rti_err_msg;
      StringUtilities::to_string( rti_err_msg, e.what() );

      send_hs( stderr, "Federate::request_federation_save_status():%d EXCEPTION: RTIinternalError: '%s' %c",
               __LINE__, rti_err_msg.c_str(), THLA_NEWLINE );
   }

   // Macro to restore the saved FPU Control Word register value.
   TRICKHLA_RESTORE_FPU_CONTROL_WORD;
   TRICKHLA_VALIDATE_FPU_CONTROL_WORD;
}

void Federate::request_federation_restore_status()
{
   if ( should_print( DEBUG_LEVEL_3_TRACE, DEBUG_SOURCE_FEDERATE ) ) {
      send_hs( stdout, "Federate::request_federation_restore_status():%d%c",
               __LINE__, THLA_NEWLINE );
   }

   // Macro to save the FPU Control Word register value.
   TRICKHLA_SAVE_FPU_CONTROL_WORD;

   try {
      RTI_ambassador->queryFederationRestoreStatus();
   } catch ( FederateNotExecutionMember &e ) {
      send_hs( stderr, "Federate::request_federation_restore_status():%d EXCEPTION: FederateNotExecutionMember %c",
               __LINE__, THLA_NEWLINE );
   } catch ( SaveInProgress &e ) {
      send_hs( stderr, "Federate::request_federation_restore_status():%d EXCEPTION: SaveInProgress %c",
               __LINE__, THLA_NEWLINE );
   } catch ( RestoreInProgress &e ) {
      send_hs( stderr, "Federate::request_federation_restore_status():%d EXCEPTION: RestoreInProgress %c",
               __LINE__, THLA_NEWLINE );
   } catch ( NotConnected &e ) {
      send_hs( stderr, "Federate::request_federation_restore_status():%d EXCEPTION: NotConnected %c",
               __LINE__, THLA_NEWLINE );
   } catch ( RTIinternalError &e ) {
      string rti_err_msg;
      StringUtilities::to_string( rti_err_msg, e.what() );

      send_hs( stderr, "Federate::request_federation_restore_status():%d EXCEPTION: RTIinternalError: '%s'%c",
               __LINE__, rti_err_msg.c_str(), THLA_NEWLINE );
   }

   // Macro to restore the saved FPU Control Word register value.
   TRICKHLA_RESTORE_FPU_CONTROL_WORD;
   TRICKHLA_VALIDATE_FPU_CONTROL_WORD;
}

/*!
 *  @job_class{freeze}
 */
void Federate::requested_federation_restore_status(
   bool status )
{
   if ( !status ) {
      if ( should_print( DEBUG_LEVEL_3_TRACE, DEBUG_SOURCE_FEDERATE ) ) {
         send_hs( stdout, "Federate::requested_federation_restore_status():%d%c",
                  __LINE__, THLA_NEWLINE );
      }

      // Macro to save the FPU Control Word register value.
      TRICKHLA_SAVE_FPU_CONTROL_WORD;

      federate_ambassador->set_federation_restore_status_response_to_echo();
      try {
         RTI_ambassador->queryFederationRestoreStatus();
      } catch ( FederateNotExecutionMember &e ) {
         send_hs( stderr, "Federate::requested_federation_restore_status():%d EXCEPTION: FederateNotExecutionMember %c",
                  __LINE__, THLA_NEWLINE );
      } catch ( SaveInProgress &e ) {
         send_hs( stderr, "Federate::requested_federation_restore_status():%d EXCEPTION: SaveInProgress %c",
                  __LINE__, THLA_NEWLINE );
      } catch ( NotConnected &e ) {
         send_hs( stderr, "Federate::requested_federation_restore_status():%d EXCEPTION: NotConnected %c",
                  __LINE__, THLA_NEWLINE );
      } catch ( RTIinternalError &e ) {
         send_hs( stderr, "Federate::requested_federation_restore_status():%d EXCEPTION: RTIinternalError %c",
                  __LINE__, THLA_NEWLINE );
      }

      // Macro to restore the saved FPU Control Word register value.
      TRICKHLA_RESTORE_FPU_CONTROL_WORD;
      TRICKHLA_VALIDATE_FPU_CONTROL_WORD;
   }
}

void Federate::print_requested_federation_restore_status(
   FederateRestoreStatusVector const &status_vector )
{
   FederateRestoreStatusVector::const_iterator vector_iter;

   // dump the contents...
   ostringstream msg;
   // load the first element from 'theFederateStatusVector'.
   vector_iter = status_vector.begin();

   // Determine if were successful.
   while ( vector_iter != status_vector.end() ) {

      // dump the contents, for now...
      string name;
      StringUtilities::to_string( name, vector_iter->preRestoreHandle );
      msg << "Federate::print_requested_federation_restore_status() "
          << __LINE__
          << "pre-restore fed_id=" << name;
      StringUtilities::to_string( name, vector_iter->postRestoreHandle );
      msg << ", post-restore fed_id =" << name
          << ", status matrix: \n   NO_RESTORE_IN_PROGRESS="
          << ( vector_iter->status == NO_RESTORE_IN_PROGRESS )
          << "\n   FEDERATE_RESTORE_REQUEST_PENDING="
          << ( vector_iter->status == FEDERATE_RESTORE_REQUEST_PENDING )
          << "\n   FEDERATE_WAITING_FOR_RESTORE_TO_BEGIN="
          << ( vector_iter->status == FEDERATE_WAITING_FOR_RESTORE_TO_BEGIN )
          << "\n   FEDERATE_PREPARED_TO_RESTORE="
          << ( vector_iter->status == FEDERATE_PREPARED_TO_RESTORE )
          << "\n   FEDERATE_RESTORING="
          << ( vector_iter->status == FEDERATE_RESTORING )
          << "\n   FEDERATE_WAITING_FOR_FEDERATION_TO_RESTORE="
          << ( vector_iter->status == FEDERATE_WAITING_FOR_FEDERATION_TO_RESTORE )
          << endl;
      // load the next element from 'theFederateStatusVector'.
      ++vector_iter;
   }
   send_hs( stdout, (char *)msg.str().c_str() );
}

void Federate::process_requested_federation_restore_status(
   FederateRestoreStatusVector const &status_vector )
{
   FederateRestoreStatusVector::const_iterator vector_iter;
   FederateRestoreStatusVector::const_iterator vector_end;
   vector_iter = status_vector.begin();
   vector_end  = status_vector.end();

   // DANNY2.7 if any of our federates have a restore in progress, we will NOT initiate restore
   this->initiate_restore_flag = true;

   // while there are elements in Federate Restore Status Vector...
   while ( vector_iter != vector_end ) {
      if ( vector_iter->status != NO_RESTORE_IN_PROGRESS ) {
         this->initiate_restore_flag = false;
         break;
      }
      ++vector_iter;
   }

   // only initiate if all federates do not have restore in progress
   if ( this->initiate_restore_flag ) {
      this->restore_process = Initiate_Restore;
   }

   // indicate that the request has completed...
   restore_request_complete = true;
}

void Federate::process_requested_federation_save_status(
   FederateHandleSaveStatusPairVector const &status_vector )
{
   FederateHandleSaveStatusPairVector::const_iterator vector_iter;
   FederateHandleSaveStatusPairVector::const_iterator vector_end;
   vector_iter = status_vector.begin();
   vector_end  = status_vector.end();

   // DANNY2.7 if any of our federates have a save in progress, we will NOT initiate save
   initiate_save_flag = true;

   // while there are elements in Federate Save Status Vector...
   while ( initiate_save_flag && ( vector_iter != vector_end ) ) {
      if ( vector_iter->second != RTI1516_NAMESPACE::NO_SAVE_IN_PROGRESS ) {
         initiate_save_flag = false;
      }
      ++vector_iter;
   }

   // indicate that the request has completed...
   save_request_complete = true;
}

void Federate::print_restore_failure_reason(
   RestoreFailureReason reason )
{
   // dump the contents...
   ostringstream msg;

   if ( reason == RTI_UNABLE_TO_RESTORE ) {
      msg << "Federate::print_restore_failure_reason():" << __LINE__
          << " failure reason=\"RTI_UNABLE_TO_RESTORE\"\n";
   }
   if ( reason == FEDERATE_REPORTED_FAILURE_DURING_RESTORE ) {
      msg << "Federate::print_restore_failure_reason():" << __LINE__
          << " failure reason=\"FEDERATE_REPORTED_FAILURE_DURING_RESTORE\"\n";
   }
   if ( reason == FEDERATE_RESIGNED_DURING_RESTORE ) {
      msg << "Federate::print_restore_failure_reason():" << __LINE__
          << " failure reason=\"FEDERATE_RESIGNED_DURING_RESTORE\"\n";
   }
   if ( reason == RTI_DETECTED_FAILURE_DURING_RESTORE ) {
      msg << "Federate::print_restore_failure_reason():" << __LINE__
          << " failure reason=\"RTI_DETECTED_FAILURE_DURING_RESTORE\"\n";
   }
   send_hs( stdout, (char *)msg.str().c_str() );

   federation_restore_failed_callback_complete = true;
}

void Federate::print_save_failure_reason(
   SaveFailureReason reason )
{
   // dump the contents...
   ostringstream msg;

   if ( reason == RTI_UNABLE_TO_SAVE ) {
      msg << "Federate::print_save_failure_reason():" << __LINE__
          << " failure reason=\"RTI_UNABLE_TO_SAVE\"\n";
   }
   if ( reason == FEDERATE_REPORTED_FAILURE_DURING_SAVE ) {
      msg << "Federate::print_save_failure_reason():" << __LINE__
          << " failure reason=\"FEDERATE_REPORTED_FAILURE_DURING_SAVE\"\n";
   }
   if ( reason == FEDERATE_RESIGNED_DURING_SAVE ) {
      msg << "Federate::print_save_failure_reason():" << __LINE__
          << " failure reason=\"FEDERATE_RESIGNED_DURING_SAVE\"\n";
   }
   if ( reason == RTI_DETECTED_FAILURE_DURING_SAVE ) {
      msg << "Federate::print_save_failure_reason():" << __LINE__
          << " failure reason=\"=RTI_DETECTED_FAILURE_DURING_SAVE\"\n";
   }
   if ( reason == SAVE_TIME_CANNOT_BE_HONORED ) {
      msg << "Federate::print_save_failure_reason():" << __LINE__
          << " failure reason=\"SAVE_TIME_CANNOT_BE_HONORED\"\n";
   }
   send_hs( stdout, (char *)msg.str().c_str() );
}

/*!
 *  @job_class{environment}
 */
void Federate::set_checkpoint_file_name(
   const char *name ) // IN: -- checkpoint file name
{
   strncpy( this->checkpoint_file_name, name, sizeof( this->checkpoint_file_name ) );
   StringUtilities::to_wstring( save_name, name );
}

/*!
 *  @job_class{environment}
 */
void Federate::initiate_save_announce()
{
   // Just return if HLA save and restore is not supported by the simulation
   // initialization scheme selected by the user.
   if ( !is_HLA_save_and_restore_supported() ) {
      return;
   }

   if ( save_label_generated ) {
      if ( should_print( DEBUG_LEVEL_2_TRACE, DEBUG_SOURCE_FEDERATE ) ) {
         send_hs( stderr, "Federate::initiate_save_announce():%d save_label already generated for federate '%s'%c",
                  __LINE__, name, THLA_NEWLINE );
      }
      return;
   }

   if ( should_print( DEBUG_LEVEL_2_TRACE, DEBUG_SOURCE_FEDERATE ) ) {
      send_hs( stdout, "Federate::initiate_save_announce():%d Checkpoint filename:'%s'%c",
               __LINE__, checkpoint_file_name, THLA_NEWLINE );
   }

   // save the checkpoint_file_name into 'save_label' class data
   // C String
   strncpy( this->cstr_save_label, checkpoint_file_name, sizeof( this->cstr_save_label ) );

   // C++ String
   this->str_save_label = checkpoint_file_name;

   // Wide String
   StringUtilities::to_wstring( this->ws_save_label, this->str_save_label );

   save_label_generated = true;
}

void Federate::initiate_restore_announce(
   const char *restore_name )
{
   // Just return if HLA save and restore is not supported by the simulation
   // initialization scheme selected by the user.
   if ( !is_HLA_save_and_restore_supported() ) {
      return;
   }

   // C String
   strncpy( this->cstr_restore_label, restore_name, sizeof( this->cstr_restore_label ) );

   // C++ String
   this->str_restore_label = TMM_strdup( (char *)restore_name );

   // Wide String
   StringUtilities::to_wstring( ws_restore_label, this->str_restore_label );

   // Macro to save the FPU Control Word register value.
   TRICKHLA_SAVE_FPU_CONTROL_WORD;

   // figure out if anybody else requested a RESTORE before initiating the RESTORE!
   // change context to process for the status request...
   restore_request_complete = false;
   federate_ambassador->set_federation_restore_status_response_to_process();
   request_federation_restore_status();
   wait_for_restore_status_to_complete();

   if ( this->restore_process == Initiate_Restore ) {
      if ( should_print( DEBUG_LEVEL_2_TRACE, DEBUG_SOURCE_FEDERATE ) ) {
         send_hs( stdout, "Federate::initiate_restore_announce():%d \
restore_process == Initiate_Restore, Telling RTI to request federation \
restore with label '%ls'.%c",
                  __LINE__, this->ws_restore_label.c_str(), THLA_NEWLINE );
      }
      try {
         RTI_ambassador->requestFederationRestore( this->ws_restore_label );
         this->restore_process = Restore_In_Progress;

         // Save the # of running_feds at the time federation restore is initiated.
         // this way, when the count decreases, we know someone has resigned!
         running_feds_count_at_time_of_restore = running_feds_count;
      } catch ( FederateNotExecutionMember &e ) {
         send_hs( stderr, "Federate::initiate_restore_announce():%d EXCEPTION: FederateNotExecutionMember %c",
                  __LINE__, THLA_NEWLINE );
         this->restore_process = No_Restore;
      } catch ( SaveInProgress &e ) {
         send_hs( stderr, "Federate::initiate_restore_announce():%d EXCEPTION: SaveInProgress %c",
                  __LINE__, THLA_NEWLINE );
         this->restore_process = No_Restore;
      } catch ( RestoreInProgress &e ) {
         send_hs( stderr, "Federate::initiate_restore_announce():%d EXCEPTION: RestoreInProgress %c",
                  __LINE__, THLA_NEWLINE );
      } catch ( NotConnected &e ) {
         send_hs( stderr, "Federate::initiate_restore_announce():%d EXCEPTION: NotConnected %c",
                  __LINE__, THLA_NEWLINE );
      } catch ( RTIinternalError &e ) {
         string rti_err_msg;
         StringUtilities::to_string( rti_err_msg, e.what() );

         send_hs( stderr, "Federate::initiate_restore_announce():%d EXCEPTION: RTIinternalError: '%s'%c",
                  __LINE__, rti_err_msg.c_str(), THLA_NEWLINE );
         this->restore_process = No_Restore;
      }
   } else {
      if ( should_print( DEBUG_LEVEL_2_TRACE, DEBUG_SOURCE_FEDERATE ) ) {
         send_hs( stderr, "Federate::initiate_restore_announce():%d \
After communicating with RTI, restore_process != Initiate_Restore, \
Something went WRONG! %c",
                  __LINE__, THLA_NEWLINE );
      }
   }

   // Macro to restore the saved FPU Control Word register value.
   TRICKHLA_RESTORE_FPU_CONTROL_WORD;
   TRICKHLA_VALIDATE_FPU_CONTROL_WORD;
}

void Federate::complete_restore()
{
   if ( should_print( DEBUG_LEVEL_3_TRACE, DEBUG_SOURCE_FEDERATE ) ) {
      send_hs( stdout, "Federate::complete_restore():%d%c",
               __LINE__, THLA_NEWLINE );
   }

   if ( this->restore_process != Restore_In_Progress ) {
      if ( should_print( DEBUG_LEVEL_3_TRACE, DEBUG_SOURCE_FEDERATE ) ) {
         send_hs( stdout, "Federate::complete_restore():%d Restore Process != Restore_In_Progress.%c",
                  __LINE__, THLA_NEWLINE );
      }
      return;
   }

   if ( !this->start_to_restore ) {
      if ( should_print( DEBUG_LEVEL_3_TRACE, DEBUG_SOURCE_FEDERATE ) ) {
         send_hs( stdout, "Federate::complete_restore():%d Start to restore flag is false so set restore_completed = true.%c",
                  __LINE__, THLA_NEWLINE );
      }
      restore_completed = true;
   }
}

bool Federate::is_federate_executing() const
{
   // Check if the manager has set a flag that the federate initialization has
   // completed and the federate is now executing.
   return execution_has_begun;
}

bool Federate::is_MOM_HLAfederation_instance_id(
   ObjectInstanceHandle instance_hndl )
{
   return ( mom_HLAfederation_instance_name_map.find( instance_hndl ) != mom_HLAfederation_instance_name_map.end() );
}

void Federate::set_MOM_HLAfederation_instance_attributes(
   ObjectInstanceHandle           instance_hndl,
   AttributeHandleValueMap const &values )
{
   // Determine if this is a MOM HLAfederation instance.
   if ( !is_MOM_HLAfederation_instance_id( instance_hndl ) ) {
      if ( should_print( DEBUG_LEVEL_2_TRACE, DEBUG_SOURCE_FEDERATE ) ) {
         send_hs( stdout, "Federate::set_federation_instance_attributes():%d WARNING: Unknown object class, expected 'HLAmanager.HLAfederation'.%c",
                  __LINE__, THLA_NEWLINE );
      }
      return;
   }

   AttributeHandleValueMap::const_iterator attr_iter;
   for ( attr_iter = values.begin(); attr_iter != values.end(); ++attr_iter ) {

      if ( attr_iter->first == MOM_HLAautoProvide_handle ) {
         // HLAautoProvide attribute is an HLAswitch, which is an HLAinteger32BE.
         int *data               = (int *)attr_iter->second.data();
         int  auto_provide_state = Utilities::is_transmission_byteswap( ENCODING_BIG_ENDIAN ) ? Utilities::byteswap_int( data[0] ) : data[0];

         if ( should_print( DEBUG_LEVEL_2_TRACE, DEBUG_SOURCE_FEDERATE ) ) {
            send_hs( stdout, "Federate::set_federation_instance_attributes():%d Auto-Provide:%s value:%d%c",
                     __LINE__, ( ( auto_provide_state != 0 ) ? "Yes" : "No" ),
                     auto_provide_state, THLA_NEWLINE );
         }

         this->auto_provide_setting = auto_provide_state;

      } else if ( attr_iter->first == MOM_HLAfederatesInFederation_handle ) {

         // Extract the size of the data and the data bytes.
         int *data = (int *)attr_iter->second.data();

         // The HLAfederatesInFederation has the HLAhandle datatype which has
         // the HLAvariableArray encoding with an HLAbyte element type. The
         // entry is the number of elements, followed by that number of
         // HLAvariableArrays.
         //  0 0 0 2 0 0 0 4 0 0 0 3 0 0 0 4 0 0 0 2
         //  ---+--- | | | | ---+--- | | | | ---+---
         //     |    ---+---    |    ---+---    |
         //   count   size   id #1    size   id #2
         //
         // The first 4 bytes (first 32-bit integer) is the number
         // of elements. WE ARE INTERESTED ONLY IN THIS VALUE!
         //
         // Determine if we need to byteswap or not since the FederateHandle
         // is in Big Endian. First 4 bytes (first 32-bit integer) is the number
         // of elements.
         int num_elements = Utilities::is_transmission_byteswap( ENCODING_BIG_ENDIAN ) ? Utilities::byteswap_int( data[0] ) : data[0];

         // save the count into running_feds_count
         running_feds_count = num_elements;

         // Since this list of federate id's is current, there is no reason to
         // thrash the RTI and chase down each federate id into a name. The
         // wait_for_required_federates_to_join() method already queries the
         // names from the RTI for all required federates. We will eventually
         // utilize the same MOM interface to rebuild this list...

         if ( should_print( DEBUG_LEVEL_2_TRACE, DEBUG_SOURCE_FEDERATE ) ) {
            send_hs( stdout, "Federate::set_federation_instance_attributes():%d Found a FederationID list with %d elements.%c",
                     __LINE__, num_elements, THLA_NEWLINE );
         }
      }
   }
}

/*!
 *  @job_class{checkpoint}
 */
void Federate::convert_sync_pts()
{

   // Dispatch to the ExecutionControl specific process.
   execution_control->convert_loggable_sync_pts();

   return;
}

void Federate::reinstate_logged_sync_pts()
{

   // Dispatch to the ExecutionControl specific process.
   execution_control->reinstate_logged_sync_pts();

   return;
}

void Federate::check_HLA_save_directory()
{
   // If the save directory is not specified, set it to the current RUN directory
   if ( this->HLA_save_directory == NULL ) {

      string run_dir = command_line_args_get_output_dir();
      string def_dir = command_line_args_get_default_dir();

      // build a absolute path to the RUN directory by combining default_dir
      // and run_dir from the EXECUTIVE.
      string new_dir = def_dir + "/" + run_dir;

      // copy the absolute path into 'HLA_save_directory'...
      HLA_save_directory = TMM_strdup( (char *)new_dir.c_str() );
   }
}

void Federate::restore_federate_handles_from_MOM()
{
   if ( should_print( DEBUG_LEVEL_2_TRACE, DEBUG_SOURCE_FEDERATE ) ) {
      send_hs( stdout, "Federate::restore_federate_handles_from_MOM:%d %c",
               __LINE__, THLA_NEWLINE );
   }

   // Make sure that we are in federate handle rebuild mode...
   federate_ambassador->set_federation_restored_rebuild_federate_handle_set();

   // Make sure we initialize the MOM handles we will use below. This should
   // also handle the case if the handles change after a checkpoint restore or
   // if this federate is now a master federate after the restore.
   initialize_MOM_handles();

   // Clear the federate handle set
   joined_federate_handles.clear();

   AttributeHandleSet fedMomAttributes;
   fedMomAttributes.insert( MOM_HLAfederate_handle );
   subscribe_attributes( MOM_HLAfederate_class_handle, fedMomAttributes );

   AttributeHandleSet requestedAttributes;
   requestedAttributes.insert( MOM_HLAfederate_handle );
   request_attribute_update( MOM_HLAfederate_class_handle, requestedAttributes );

   unsigned int sleep_micros = 1000;
   unsigned int wait_count   = 0;
   unsigned int wait_check   = 10000000 / sleep_micros; // Number of wait cycles for 10 seconds

   // Wait until all of the federate handles have been retrieved.
   while ( joined_federate_handles.size() < (unsigned int)running_feds_count ) {
      usleep( sleep_micros );

      if ( ( joined_federate_handles.size() < (unsigned int)running_feds_count ) && ( ( ++wait_count % wait_check ) == 0 ) ) {
         wait_count = 0;
         if ( !is_execution_member() ) {
            ostringstream errmsg;
            errmsg << "Federate::restore_federate_handles_from_MOM():" << __LINE__
                   << " Unexpectedly the Federate is no longer an execution member."
                   << " This means we are either not connected to the"
                   << " RTI or we are no longer joined to the federation"
                   << " execution because someone forced our resignation at"
                   << " the Central RTI Component (CRC) level!"
                   << THLA_ENDL;
            send_hs( stderr, (char *)errmsg.str().c_str() );
            exec_terminate( __FILE__, (char *)errmsg.str().c_str() );
         }
      }
   }

   // Only unsubscribe from the attributes we subscribed to in this function.
   unsubscribe_attributes( MOM_HLAfederate_class_handle, fedMomAttributes );
   //   unsubscribe_all_HLAfederate_class_attributes_from_MOM();

   // Make sure that we are no longer in federate handle rebuild mode...
   federate_ambassador->reset_federation_restored_rebuild_federate_handle_set();
}

void Federate::rebuild_federate_handles(
   ObjectInstanceHandle           instance_hndl,
   AttributeHandleValueMap const &values )
{
   AttributeHandleValueMap::const_iterator attr_iter;

   // loop through all federate handles
   for ( attr_iter = values.begin(); attr_iter != values.end(); ++attr_iter ) {

      // Do a sanity check on the overall encoded data size.
      if ( attr_iter->second.size() != 8 ) {
         ostringstream errmsg;
         errmsg << "Federate::rebuild_federate_handles():"
                << __LINE__ << " Unexpected number of bytes in the"
                << " Encoded FederateHandle because the byte count is "
                << attr_iter->second.size()
                << " but we expected 8!" << THLA_ENDL;
         send_hs( stderr, (char *)errmsg.str().c_str() );
         exec_terminate( __FILE__, (char *)errmsg.str().c_str() );
         exit( 1 );
      }

      // The HLAfederateHandle has the HLAhandle datatype which is has the
      // HLAvariableArray encoding with an HLAbyte element type.
      //  0 0 0 4 0 0 0 2
      //  ---+--- | | | |
      //     |    ---+---
      // #elem=4  fedID = 2
      //
      // First 4 bytes (first 32-bit integer) is the number of elements.
      // Decode size from Big Endian encoded integer.
      unsigned char *dataPtr = (unsigned char *)attr_iter->second.data();
      //int size = ntohl( *(unsigned int *)dataPtr );
      size_t size = Utilities::is_transmission_byteswap( ENCODING_BIG_ENDIAN ) ? (size_t)Utilities::byteswap_int( *(int *)dataPtr ) : ( size_t ) * (int *)dataPtr;
      if ( size != 4 ) {
         ostringstream errmsg;
         errmsg << "Federate::rebuild_federate_handles():"
                << __LINE__ << "FederateHandle size is "
                << size << " but expected it to be 4!" << THLA_ENDL;
         send_hs( stderr, (char *)errmsg.str().c_str() );
         exec_terminate( __FILE__, (char *)errmsg.str().c_str() );
         exit( 1 );
      }

      // Point to the start of the federate handle ID in the encoded data.
      dataPtr += 4;

      VariableLengthData t;
      t.setData( dataPtr, size );

      FederateHandle tHandle;

      // Macro to save the FPU Control Word register value.
      TRICKHLA_SAVE_FPU_CONTROL_WORD;

      try {
         tHandle = RTI_ambassador->decodeFederateHandle( t );
      } catch ( CouldNotDecode &e ) {
         // Macro to restore the saved FPU Control Word register value.
         TRICKHLA_RESTORE_FPU_CONTROL_WORD;
         TRICKHLA_VALIDATE_FPU_CONTROL_WORD;

         ostringstream errmsg;
         errmsg << "Federate::rebuild_federate_handles():" << __LINE__
                << " EXCEPTION: CouldNotDecode" << THLA_ENDL;
         send_hs( stderr, (char *)errmsg.str().c_str() );
         exec_terminate( __FILE__, (char *)errmsg.str().c_str() );
         exit( 1 );
      } catch ( FederateNotExecutionMember &e ) {
         // Macro to restore the saved FPU Control Word register value.
         TRICKHLA_RESTORE_FPU_CONTROL_WORD;
         TRICKHLA_VALIDATE_FPU_CONTROL_WORD;

         ostringstream errmsg;
         errmsg << "Federate::rebuild_federate_handles():" << __LINE__
                << " EXCEPTION: FederateNotExecutionMember" << THLA_ENDL;
         send_hs( stderr, (char *)errmsg.str().c_str() );
         exec_terminate( __FILE__, (char *)errmsg.str().c_str() );
         exit( 1 );
      } catch ( NotConnected &e ) {
         // Macro to restore the saved FPU Control Word register value.
         TRICKHLA_RESTORE_FPU_CONTROL_WORD;
         TRICKHLA_VALIDATE_FPU_CONTROL_WORD;

         ostringstream errmsg;
         errmsg << "Federate::rebuild_federate_handles():" << __LINE__
                << " EXCEPTION: NotConnected" << THLA_ENDL;
         send_hs( stderr, (char *)errmsg.str().c_str() );
         exec_terminate( __FILE__, (char *)errmsg.str().c_str() );
         exit( 1 );
      } catch ( RTIinternalError &e ) {
         // Macro to restore the saved FPU Control Word register value.
         TRICKHLA_RESTORE_FPU_CONTROL_WORD;
         TRICKHLA_VALIDATE_FPU_CONTROL_WORD;

         string rti_err_msg;
         StringUtilities::to_string( rti_err_msg, e.what() );

         ostringstream errmsg;
         errmsg << "Federate::rebuild_federate_handles():" << __LINE__
                << " EXCEPTION: RTIinternalError: %s" << rti_err_msg << THLA_ENDL;
         send_hs( stderr, (char *)errmsg.str().c_str() );
         exec_terminate( __FILE__, (char *)errmsg.str().c_str() );
         exit( 1 );
      }

      // Macro to restore the saved FPU Control Word register value.
      TRICKHLA_RESTORE_FPU_CONTROL_WORD;
      TRICKHLA_VALIDATE_FPU_CONTROL_WORD;

      // Add this FederateHandle to the set of joined federates.
      joined_federate_handles.insert( tHandle );

      if ( should_print( DEBUG_LEVEL_2_TRACE, DEBUG_SOURCE_FEDERATE ) ) {
         string id_str, fed_id;
         StringUtilities::to_string( id_str, instance_hndl );
         StringUtilities::to_string( fed_id, tHandle );
         send_hs( stdout, "Federate::rebuild_federate_handles():%d Federate OID:%s num_bytes:%d Federate-ID:%s%c",
                  __LINE__, id_str.c_str(), size, fed_id.c_str(), THLA_NEWLINE );
      }
   }
   return;
}

/*!
 * @details Returns true if the supplied name is a required startup federate
 * or an instance object of a required startup federate.
 * \par<b>Assumptions and Limitations:</b>
 * - Assumes that the instance attributes' object name is in the format
 * 'object_name.FOM_name'. Otherwise, this logic fails.
 */
bool Federate::is_a_required_startup_federate(
   wstring const &fed_name )
{
   wstring required_fed_name;
   for ( int i = 0; i < known_feds_count; ++i ) {
      if ( known_feds[i].required ) {
         StringUtilities::to_wstring( required_fed_name, known_feds[i].name );
         if ( fed_name == required_fed_name ) { // found an exact match
            return true;
         } else {
            // look for instance attributes of a required object. to do this, check if the
            // "required federate name" is found inside the supplied federate name.
            size_t found = fed_name.find( required_fed_name );
            if ( found != wstring::npos ) { // found the "required federate name" inside the supplied federate name
               return true;
            }
         }
      }
   }
   return false;
}