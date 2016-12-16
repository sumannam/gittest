/*******************************************************************************
** Copyright (c) 2004 MaK Technologies, Inc.
** All rights reserved.
*******************************************************************************/

// A simple federate that updates an object with attributes whose values
// are the name of the attribute. Objects from other simple federates
// are discovered and reflected. The string values are byte encoded to allow
// compatibility between 1.3 and 1516

#pragma warning(disable: 4786)
#pragma warning(disable: 4290)

#ifdef WIN32
#include <winsock2.h>
#include <process.h>
#else
#include <unistd.h>
#endif

#include <stdio.h>
#include <cstdlib>
#include <cstring>

#include <map>
#include <string>
#include <iostream>
#include <sstream>

#include "RTI.hh"
#include "rtiSimpleFedAmb13.h"
#include "rtiSimpleKeyboard.h"

using namespace std;

// Handles keyboard input without blocking
keyboard input;

// Map between strings and attribute handles
typedef std::map<std::string, RTI::AttributeHandle>  DtAttrNameHandleMap;

// Map between strings and parameter handles
typedef std::map<std::string, RTI::ParameterHandle>  DtParamNameHandleMap;

// Data shared between federate and federate ambassador
DtTalkAmbData theAmbData;

// The federate handle
RTI::FederateHandle theFederateHandle;

// The object class name
std::string theClassName = "BaseEntity";

// The interaction class name
std::string theInterClassName = "WeaponFire";

// The object class handle (to be retrieved from RTI).
RTI::ObjectClassHandle theClassHandle;

// The interaction class handle (to be retrieved from RTI).
RTI::InteractionClassHandle theInterClassHandle;

// The object instance handle (to be retrieved from the RTI).
RTI::ObjectHandle theObjectHandle;

// Map between strings and attribute handles
DtAttrNameHandleMap theAttrNameHandleMap;

// Map between strings and parameter handles
DtParamNameHandleMap theParamNameHandleMap;

RTI::AttributeHandle myName;
RTI::AttributeHandle myNum;

static const std::string theName = "HELLO";
static const std::string theNum = "NUM";

std::string attrName;
std::string attrNum;

RTI::AttributeHandleSet* myAllBallAttrs;

//static const std::string theClass;

//const std::string theBallClassName="Test";
//const std::string theMsg="HELLO";
//const std::string theNum="NUM";

////////////////////////////////////////////////////////////////////////////////
// Create the federation execution
void createFedEx(RTI::RTIambassador & rtiAmb,
                 std::string const& fedName,
                 std::string const& fedFile)
{
   cout  << "createFederationExecution "
         << fedName.c_str() << " "
         << fedFile.c_str() << endl;
   try
   {      
      rtiAmb.createFederationExecution(fedName.c_str(), fedFile.c_str());
   }
   catch(RTI::FederationExecutionAlreadyExists& ex)
   {
      cout  << "Could not create Federation Execution: "
            << "FederationExecutionAlreadyExists: "
            <<  ex._name << " "
            << ex._reason << endl;
   }
   catch(RTI::Exception& ex)
   {
      cout  << "Could not create Federation Execution: " << endl
            << "RTI Exception: "
            << ex._name << " "
            << ex._reason << endl;
      exit(0);
   }
   rtiAmb.tick(0.1, 0.2);

   cout << "Federation Created" << endl;
}

////////////////////////////////////////////////////////////////////////////////
// Join the federation execution
void joinFedEx(
   RTI::RTIambassador & rtiAmb, MyFederateAmbassador* fedAmb,
   std::string const& federateType, std::string const& federationName)
{
   bool joined=false;
   const int maxTry  = 10;
   int numTries = 0;
   cout  << "joinFederationExecution "
         << federateType.c_str() << " "
         << federationName.c_str() << endl;

   while (!joined && numTries++ < maxTry)
   {
      try
      {
         theFederateHandle = rtiAmb.joinFederationExecution(federateType.c_str(),
            federationName.c_str(), fedAmb);
         joined = true;
      }
      catch(RTI::FederationExecutionDoesNotExist)
      {
         cout  << "FederationExecutionDoesNotExist, try "
               << numTries << "out of "
               << maxTry << endl;
         continue;
      }
      catch(RTI::Exception& ex)
      {
         cout  << "RTI Exception: "
               << ex._name << " "
               << ex._reason << endl;
         return;
      }
      rtiAmb.tick(0.1, 0.2);
   }
   if (joined)
      cout << "Joined Federation." << endl;
   else
   {
      cout << "Giving up." << endl;
      rtiAmb.destroyFederationExecution(federationName.c_str());
      exit(0);
   }
}

////////////////////////////////////////////////////////////////////////////////
// Resign and destroy the federation execution
void resignAndDestroy( RTI::RTIambassador & rtiAmb,
      std::string const& federationName)
{
   cout << "Resign and Destroy Federation" << endl;
   rtiAmb.resignFederationExecution(RTI::DELETE_OBJECTS);
   rtiAmb.destroyFederationExecution(federationName.c_str());
}

////////////////////////////////////////////////////////////////////////////////
// Save federation

// Request federation save and wait for initiate
// Return save status
bool requestFederationSave(RTI::RTIambassador & rtiAmb, std::string const& label)
{
   bool saveOk = true;
   int count = 0;

   // Turn the signals off
   theAmbData.myReceivedInitiateFederateSave =
      theAmbData.myReceivedFederationSaved =
      theAmbData.myReceivedFederationNotSaved = false;

   cout << "Request federation save with label " << label.c_str() << endl;

   // Request the save and wait for the signal that the save has been initiated
   // or that the federation has not saved (could be that some other federate
   // resigns during save)
   rtiAmb.requestFederationSave(label.c_str());

   while (count++ < 100
      && !theAmbData.myReceivedInitiateFederateSave
      && !theAmbData.myReceivedFederationNotSaved)
   {
      rtiAmb.tick(0.1, 0.2);
   }

   if ( !theAmbData.myReceivedInitiateFederateSave )
   {
      saveOk = false;
      if ( !theAmbData.myReceivedFederationNotSaved )
      {
         cout << "Timed out waiting for initiate federate save\n";
      }
   }
   return saveOk;
}

// Wait for federation saved
// Return save status
bool waitForFederationSaved(RTI::RTIambassador & rtiAmb)
{
   bool saveOk = true;
   int count = 0;

   // Wait until the RTI signals that the entire federation completed the save

   while ( count++ < 100
      && !theAmbData.myReceivedFederationNotSaved
      && !theAmbData.myReceivedFederationSaved )
   {
      rtiAmb.tick(0.1, 0.2);
   }

   if ( !theAmbData.myReceivedFederationSaved )
   {
      saveOk = false;
      if ( !theAmbData.myReceivedFederationNotSaved )
      {
         cout << "Timed out waiting for federation saved\n";
      }
   }
   return saveOk;
}

// Perform save federation
// Return save status
bool saveFederation(RTI::RTIambassador & rtiAmb, std::string const& label, bool performRequest)
{
   bool saveOk = true;
   int count = 0;

   try
   {
      if ( performRequest )
      {
         saveOk = requestFederationSave(rtiAmb, label);
      }

      if ( theAmbData.myReceivedInitiateFederateSave )
      {
         // At this point, the save has been initiated
         // and the federate cannot invoke any other calls except
         // to complete the save (or resign). It signals to the RTI that
         // it will begin saving its own local state

         cout << "Federate save begun\n";
         rtiAmb.federateSaveBegun();

         // Here the federate would save its own state including any context
         // information relating to the RTI
         // (i.e. what classes are published and subscribed
         // object instance handles for registered and discovered objects, etc.

         // Once the federate saves is own data, it signals to the RTI that its
         // save is complete.
         cout << "Federate save complete\n";
         rtiAmb.federateSaveComplete();

         // Wait until the RTI signals that the entire federation completed the save
         saveOk = waitForFederationSaved(rtiAmb);
      }
   }
   catch (RTI::Exception& ex)
   {
      cout  << "RTI Exception: "
         << ex._name << " "
         << ex._reason << endl;
      cout << "Could not  save federation " << label.c_str() << endl;
      saveOk = false;
   }
   catch(exception& stdEx)
   {
      cout << "Standard exception: " << stdEx.what() << endl;
      cout << "Could not  save federation " << label.c_str() << endl;
      saveOk = false;
   }
   catch(...)
   {
      cout << "Unknown exception\n";
      cout << "Could not  save federation " << label.c_str() << endl;
      saveOk = false;
   }

   // Turn the signals off
   theAmbData.myReceivedInitiateFederateSave =
      theAmbData.myReceivedFederationSaved =
      theAmbData.myReceivedFederationNotSaved = false;

   return saveOk;
}

////////////////////////////////////////////////////////////////////////////////
// Restore federation

// Request the restore and wait for the response.
// Return request status
bool requestFederationRestore(RTI::RTIambassador & rtiAmb, std::string const& label)
{
   int count = 0;
   bool restoreOk = true;

   cout << "Request federation restore with label " << label.c_str() << endl;

   // Turn the signals off
   theAmbData.myReceivedRequestFederationRestoreSucceeded =
      theAmbData.myReceivedRequestFederationRestoreFailed =
      theAmbData.myReceivedFederationRestoreBegun =
      theAmbData.myReceivedInitiateFederateRestore =
      theAmbData.myReceivedFederationRestored =
      theAmbData.myReceivedFederationNotRestored = false;

   rtiAmb.requestFederationRestore(label.c_str());
   while (count++ < 100
      && !theAmbData.myReceivedRequestFederationRestoreSucceeded
      && !theAmbData.myReceivedRequestFederationRestoreFailed)
   {
      rtiAmb.tick(0.1, 0.2);
   }

   if ( !theAmbData.myReceivedRequestFederationRestoreSucceeded )
   {
      restoreOk = false;
      if ( !theAmbData.myReceivedRequestFederationRestoreFailed )
      {
         cout << "Timed out waiting for request federation restore succeeded.\n";
      }
   }
   return restoreOk;
}

// Wait for restore begun
// Return begun status
bool waitForRestoreBegun(RTI::RTIambassador & rtiAmb)
{
   bool restoreOk = true;
   // Wait for the restore begun

   cout << "Wait for restore begun\n";
   int count = 0;
   while (count++ < 100
      && !theAmbData.myReceivedFederationRestoreBegun
      && !theAmbData.myReceivedFederationNotRestored)
   {
      rtiAmb.tick(0.1, 0.2);
   }

   if ( !theAmbData.myReceivedFederationRestoreBegun )
   {
      restoreOk = false;
      if (!theAmbData.myReceivedFederationNotRestored)
      {
         cout << "Timed out waiting for federation restore begun.\n";
      }
   }
   return restoreOk;
}

// Wait for initiate restore
// Return initiate status
bool waitForInitiateRestore(RTI::RTIambassador & rtiAmb)
{
   bool restoreOk = true;

   // Wait for the initiate restore

   cout << "Wait for initiate restore\n";

   int count = 0;
   while (count++ < 100
      && !theAmbData.myReceivedInitiateFederateRestore
      && !theAmbData.myReceivedFederationNotRestored)
   {
      rtiAmb.tick(0.1, 0.2);
   }

   if ( theAmbData.myReceivedFederationNotRestored )
   {
      restoreOk = false;
   }
   else if ( !theAmbData.myReceivedInitiateFederateRestore )
   {
      restoreOk = false;
      cout << "Timed out waiting for initiate federate restore.\n";
   }
   else if ( theAmbData.myRestoreFederateHandle != theFederateHandle )
   {
      // In general, a federate must be able to restore any saved state of
      // a federate of the same type. Even if a federate submits
      // a unique federate type string during join, the current federate
      // and object handles may not match those being restored.
      // This simplistic federate implementation cannot handle the case where
      // the federate and object handles are different.
      cout << "Restore initiated with handle " << theAmbData.myRestoreFederateHandle
         << " does not match current handle " << theFederateHandle << endl;
      cout << "Unable to complete restore\n";

      restoreOk = false;

      // Will revert to state before restore was begun
      rtiAmb.federateRestoreNotComplete();

      count = 0;
      while ( count++ < 100
         && !theAmbData.myReceivedFederationNotRestored )
      {
         rtiAmb.tick(0.1, 0.2);
      }

      if ( !theAmbData.myReceivedFederationNotRestored )
      {
         cout << "Timed out waiting for federation not restored.\n";
      }
   }

   return restoreOk;
}

// Wait for federation (not) restored
// Return restore status
bool waitForFederationRestored(RTI::RTIambassador & rtiAmb)
{
   bool restoreOk = true;
   // Wait until the RTI signals that the entire federation completed the restore

   cout << "Wait for federation restored\n";
   int count = 0;
   while ( count++ < 100
      && !theAmbData.myReceivedFederationRestored
      && !theAmbData.myReceivedFederationNotRestored )
   {
      rtiAmb.tick(0.1, 0.2);
   }

   if ( !theAmbData.myReceivedFederationRestored )
   {
      restoreOk = false;
      if ( !theAmbData.myReceivedFederationNotRestored )
      {
         cout << "Timed out waiting for federation restored.\n";
      }
   }
   return restoreOk;
}

// Perform restore federation
// Return restore status
bool restoreFederation(RTI::RTIambassador & rtiAmb, std::string const& label, bool performRequest)
{
   bool restoreOk = true;
   int count = 0;

   try
   {
      if ( performRequest )
      {
         if ( requestFederationRestore(rtiAmb, label) )
         {
            restoreOk = waitForRestoreBegun(rtiAmb);
         }
      }

      if ( theAmbData.myReceivedFederationRestoreBegun )
      {
         // At this point, the restore has begun
         // and the federate cannot invoke any other calls except
         // to complete the restore (or resign). It waits for the
         // initiate restore to begin restoring its local state.

         if ( restoreOk = waitForInitiateRestore(rtiAmb) )
         {
            // Here the federate would restore its own state including any context
            // information relating to the RTI
            // (i.e. what classes are published and subscribed
            // object instance handles for registered and discovered objects, etc.

            // Once the federate restores its own data, it signals to the RTI that its
            // restore is complete.
            wcout << L"Federate restore complete\n";
            rtiAmb.federateRestoreComplete();
            // If the federate was unable to restore its state, it would respond with
            // federateRestoreNotComplete();

            // Now wait for remaining federation to be restored
            restoreOk = waitForFederationRestored(rtiAmb);
         }
      }
   }
   catch (RTI::Exception& ex)
   {
      cout  << "RTI Exception: "
         << ex._name << " "
         << ex._reason << endl;
      cout << "Could not  save federation " << label.c_str() << endl;
      restoreOk = false;
   }
   catch(exception& stdEx)
   {
      cout << "Standard exception: " << stdEx.what() << endl;
      cout << "Could not  save federation " << label.c_str() << endl;
      restoreOk = false;
   }
   catch(...)
   {
      cout << "Unknown exception\n";
      cout << "Could not  save federation " << label.c_str() << endl;
      restoreOk = false;
   }

   // Turn the signals off
   theAmbData.myReceivedRequestFederationRestoreSucceeded =
      theAmbData.myReceivedRequestFederationRestoreFailed =
      theAmbData.myReceivedFederationRestoreBegun =
      theAmbData.myReceivedInitiateFederateRestore =
      theAmbData.myReceivedFederationRestored =
      theAmbData.myReceivedFederationNotRestored = false;

   return restoreOk;
}

////////////////////////////////////////////////////////////////////////////////
// Publish and subscribe the object class attributes.
// Register an object instance of the class.
bool publishSubscribeAndRegisterObject(RTI::RTIambassador & rtiAmb)
{
   // Get the object class handle
   try
   {  
      theClassHandle = rtiAmb.getObjectClassHandle(theClassName.c_str());
      theAmbData.objectClassMap[theClassHandle] = theClassName;
   }
   catch (RTI::Exception& ex)
   {
      cout  << "RTI Exception: "
            << ex._name << " "
            << ex._reason << endl
            << "Could not get object class handle: "
            << theClassName.c_str() << endl;
      return false;
   }

   // Get the attribute handles and construct the name-handle map
   /*std::string attrName;*/
   try
   {
	   //attrName  = "HELLO";
	   //msg = rtiAmb.getAttributeHandle( attrName.c_str(), theClassHandle );

		/*attrName  = "HELLO";
		theAttrNameHandleMap[attrName] = rtiAmb.getAttributeHandle(theName.c_str(), theClassHandle);*/

		//attrName  = "NUM";
		//theAttrNameHandleMap[attrName] = rtiAmb.getAttributeHandle(theNum.c_str(), theClassHandle);
		//num = rtiAmb.getAttributeHandle( theNum.c_str(), theClassHandle );
		
	   theAttrNameHandleMap[theName]	= rtiAmb.getAttributeHandle( theName.c_str(), theClassHandle );
	   theAttrNameHandleMap[theNum]		= rtiAmb.getAttributeHandle( theNum.c_str(), theClassHandle );

	   //myAllBallAttrs = RTI::AttributeHandleSetFactory::create(2);
    //   myAllBallAttrs->add(theAttrNameHandleMap[theName]);
	   //myAllBallAttrs->add(theAttrNameHandleMap[theNum]);

  //      myName = rtiAmb.getAttributeHandle( theName.c_str(), theClassHandle );

		//myAllBallAttrs = RTI::AttributeHandleSetFactory::create(2);
		//myAllBallAttrs->add(myName);

	  /*attrName  = "AccelerationVector";
      theAttrNameHandleMap[attrName] = 
            rtiAmb.getAttributeHandle(attrName.c_str(), theClassHandle);*/
      //attrName  = "VelocityVector";
      //theAttrNameHandleMap[attrName] = 
      //      rtiAmb.getAttributeHandle(attrName.c_str(), theClassHandle);
      //attrName  = "Orientation";
      //theAttrNameHandleMap[attrName] = 
      //      rtiAmb.getAttributeHandle(attrName.c_str(), theClassHandle);
      //attrName  = "DeadReckoningAlgorithm";
      //theAttrNameHandleMap[attrName] = 
      //      rtiAmb.getAttributeHandle(attrName.c_str(), theClassHandle);
      //attrName  = "WorldLocation";
      //theAttrNameHandleMap[attrName] = 
      //      rtiAmb.getAttributeHandle(attrName.c_str(), theClassHandle);

		//myAllBallAttrs = RTI::AttributeHandleSetFactory::create(6);

		//string h = "Hello World";
		//int a = 0;
		////unsigned char MSG = static_cast<unsigned char>(h);			
		//myAllBallAttrs->add(a);
		////myAllBallAttrs->add(msg, reinterpret_cast<const char*>(&h), sizeof(h)+1);
			

   }
   catch (RTI::Exception& ex)
   {
      cout  << "RTI Exception: "
            << ex._name << " "
            << ex._reason << endl
            << "Could not get attribute handle "
            << attrName.c_str() << endl;
      return false;
   }

   // Construct an attribute handle set
   RTI::AttributeHandleSet* hSet = 
      RTI::AttributeHandleSetFactory::create(theAttrNameHandleMap.size());

   for (DtAttrNameHandleMap::iterator iter = theAttrNameHandleMap.begin();
        iter != theAttrNameHandleMap.end();
        iter++)
   {
      hSet->add(iter->second);
   }

    // Publish and subscribe
   int cnt=0;
   try
   {
	   rtiAmb.publishObjectClass(theClassHandle, *hSet);
	   rtiAmb.tick(0.1, 0.2);
	   cnt=1;
	   rtiAmb.subscribeObjectClassAttributes(theClassHandle, *hSet);
	   rtiAmb.tick(0.1, 0.2);
   }
   catch (RTI::Exception& ex)
   {
      cout  << "RTI Exception: "
            << ex._name << " "
            << ex._reason << endl
            << "Could not "
            << (cnt ? "publish" : "subscribe") << endl;
      delete hSet;
      return false;
   }

   std::string objectName("Talk");

   // Register the object instance
   try
   {
      unsigned int objId = abs(getpid());
      std::stringstream pid;
      pid << objId;
      objectName += pid.str();
      theObjectHandle = rtiAmb.registerObjectInstance(theClassHandle, objectName.c_str());
      //theObjectHandle = rtiAmb.registerObjectInstance(theClassHandle);

      // Add name-handle to map
      theAmbData.objectInstanceMap[theObjectHandle] =
         rtiAmb.getObjectInstanceName(theObjectHandle);

      rtiAmb.tick(0.1, 0.2);
   }
   catch (RTI::Exception& ex)
   {
      cout  << "RTI Exception: "
            << ex._name << " "
            << ex._reason << endl
            << "Could not  Register Object "
            << objectName.c_str()
            << " with class "
            << theClassName.c_str() << endl;
      delete hSet;
      return false;
   }

   cout  << "Registered object "
         << objectName.c_str()
         << " with class name "
         <<  theClassName.c_str() << endl;
      delete hSet;
   return true;
}


////////////////////////////////////////////////////////////////////////////////
// Publish and Subscribe to an interaction class
bool publishAndSubscribeInteraction(RTI::RTIambassador & rtiAmb)
{
   // Get the interaction class handle
   try
   {  
      theInterClassHandle = rtiAmb.getInteractionClassHandle(theInterClassName.c_str());
      theAmbData.interactionClassMap[theInterClassHandle] = theInterClassName;
   }
   catch (RTI::Exception& ex)
   {
      cout  << "RTI Exception: "
            << ex._name << " "
            << ex._reason << endl
            << "Could not get interaction class handle: "
            << theInterClassName.c_str() << endl;
      return false;
   }

   // Get the parameter handles and construct the name-handle map
   std::string paramName;
   try
   {
	  paramName  = "param_HelloWorld";
      theParamNameHandleMap[paramName] = 
            rtiAmb.getParameterHandle(paramName.c_str(), theInterClassHandle);
      /*paramName  = "EventIdentifier";
      theParamNameHandleMap[paramName] = 
            rtiAmb.getParameterHandle(paramName.c_str(), theInterClassHandle);*/
      /*paramName  = "FireControlSolutionRange";
      theParamNameHandleMap[paramName] = 
            rtiAmb.getParameterHandle(paramName.c_str(), theInterClassHandle);
      paramName  = "FireMissionIndex";
      theParamNameHandleMap[paramName] = 
            rtiAmb.getParameterHandle(paramName.c_str(), theInterClassHandle);
      paramName  = "FiringLocation";
      theParamNameHandleMap[paramName] = 
            rtiAmb.getParameterHandle(paramName.c_str(), theInterClassHandle);
      paramName  = "FiringObjectIdentifier";
      theParamNameHandleMap[paramName] = 
            rtiAmb.getParameterHandle(paramName.c_str(), theInterClassHandle);
      paramName  = "FuseType";
      theParamNameHandleMap[paramName] = 
            rtiAmb.getParameterHandle(paramName.c_str(), theInterClassHandle);
      paramName  = "InitialVelocityVector";
      theParamNameHandleMap[paramName] = 
            rtiAmb.getParameterHandle(paramName.c_str(), theInterClassHandle);
      paramName  = "MunitionObjectIdentifier";
      theParamNameHandleMap[paramName] = 
            rtiAmb.getParameterHandle(paramName.c_str(), theInterClassHandle);
      paramName  = "MunitionType";
      theParamNameHandleMap[paramName] = 
            rtiAmb.getParameterHandle(paramName.c_str(), theInterClassHandle);
      paramName  = "QuantityFired";
      theParamNameHandleMap[paramName] = 
            rtiAmb.getParameterHandle(paramName.c_str(), theInterClassHandle);
      paramName  = "RateOfFire";
      theParamNameHandleMap[paramName] = 
            rtiAmb.getParameterHandle(paramName.c_str(), theInterClassHandle);
      paramName  = "TargetObjectIdentifier";
      theParamNameHandleMap[paramName] = 
            rtiAmb.getParameterHandle(paramName.c_str(), theInterClassHandle);
      paramName  = "WarheadType";
      theParamNameHandleMap[paramName] = 
            rtiAmb.getParameterHandle(paramName.c_str(), theInterClassHandle);*/
   }
   catch (RTI::Exception& ex)
   {
      cout  << "RTI Exception: "
            << ex._name << " "
            << ex._reason << endl
            << "Could not get parameter handle "
            << paramName.c_str() << endl;
      return false;
   }

    // Publish and subscribe
   int cnt=0;
   try
   {
   rtiAmb.publishInteractionClass(theInterClassHandle);
   rtiAmb.tick(0.1, 0.2);
   cnt=1;
   rtiAmb.subscribeInteractionClass(theInterClassHandle);
   rtiAmb.tick(0.1, 0.2);
   }
   catch (RTI::Exception& ex)
   {
      cout  << "RTI Exception: "
            << ex._name << " "
            << ex._reason << endl
            << "Could not "
            << (cnt ? "publish" : "subscribe")
            << " to interaction." << endl;
      return false;
   }

   cout  << "Subscribed to interaction class: "
         << theInterClassName.c_str()
         << " with handle: "
         <<  theInterClassHandle << endl;
   return true;
}

int main(int argc, char** argv)
{
   std::cout << "MAK rtiSimple version 1.3" << std::endl;


   try
   {
      // Federate and Federation info
      std::string federationName("MAKsimple");
      std::string federationFile("MAKsimple.fed");
      std::string federateType("rtisimple13");

      if (argc > 1)
      {
         if (strcmp(argv[1], "-h") == 0)
         {
            cout  << "Usage: rtisimple13 [federationName] [federationFile] [federateType]" << endl;
            cout  << "default values: "
                  << "\"" << federationName.c_str() << "\" "
                  << "\"" << federationFile.c_str() <<  "\" "
                  << "\"" << federateType.c_str() <<  "\" " << endl;
            return 0;
         }
         federationName = argv[1];
      }

      if (argc > 2)
      {
         federationFile = argv[2];
      }

      if (argc > 3)
      {
         federateType = argv[3];
      }

      // RTI and Federate Ambassadors
      RTI::RTIambassador rtiAmb;
      MyFederateAmbassador fedAmb(theAmbData);

      RTI::AttributeHandleValuePairSet *attrValues = 0;
      RTI::ParameterHandleValuePairSet *paramValues = 0;

#ifdef WIN32
      WSADATA data;
      WSAStartup(MAKEWORD(1,1), &data);
#endif
      rtiAmb.tick();
      long count=0;
      bool doConnect = true;
      bool connected = false;
      while (1)
      {
         if (doConnect)
         {
            doConnect = false;

            // Create the federation
            createFedEx(rtiAmb, federationName, federationFile);

            // Join the federation
            joinFedEx(rtiAmb, &fedAmb, federateType, federationName);

            // Publish, subscribe and register and object
            if (!publishSubscribeAndRegisterObject(rtiAmb))
            {
               resignAndDestroy(rtiAmb, federationName);
               return 0;
            }

            // Publish and subscribe to the required interaction
            if (!publishAndSubscribeInteraction(rtiAmb))
            {
               resignAndDestroy(rtiAmb, federationName);
               return 0;
            }

            // Construct an attribute handle value pair set with the
            // values containing the attribute names
            attrValues = RTI::AttributeSetFactory::create(theAttrNameHandleMap.size());
			const char msg_[10] = "hello";
            for (DtAttrNameHandleMap::iterator iter = theAttrNameHandleMap.begin();
               iter != theAttrNameHandleMap.end();
               iter++)
            {
				//attrValues->add(iter->second, iter->first.c_str(), iter->first.length()+1);
				attrValues->add(iter->second, msg_, iter->first.length()+1);
            }

            // Construct a parameter handle value pair set with the
            // values containing the parameter names
            paramValues = RTI::ParameterSetFactory::create(theParamNameHandleMap.size());
			
            for (DtParamNameHandleMap::iterator iter2 = theParamNameHandleMap.begin();
               iter2 != theParamNameHandleMap.end();
               iter2++)
            {
               // Parameter values will be just the name of the attribute.
				//paramValues->add(iter2->second, iter->first.c_str(), iter2->first.length()+1);
               paramValues->add(iter2->second, msg_, iter2->first.length()+1);
            }
            connected = true;
         }
         else if (connected)
         {
            std::stringstream ss;
            //ss << "1.3-" << count++;
			ss << "HLA/RTI: " << count++;
            std::string tag(ss.str());
			//attrValues->empty();
            // Update the object

			//attrValues->empty();

			//std::string h = "Hello World";
			//std::string msg = "Hello World";

			const char msg[10] = "hello";

			//int num = 0;
			//int n = htons(num);		
			//attrValues->add( myNum,	reinterpret_cast<const char*>(&n), sizeof(n));
			
            rtiAmb.updateAttributeValues( theObjectHandle, *attrValues, tag.c_str());

			cout << "Sending interaction..." << endl;
              rtiAmb.sendInteraction( theInterClassHandle, *paramValues, tag.c_str());

            // Send an interaction every few passes
            //if ( count % 5 == 0 )  
            //{
            //   cout << "Sending interaction..." << endl;
            //   rtiAmb.sendInteraction(
            //      theInterClassHandle,
            //      *paramValues,
            //      tag.c_str());
            //}

            rtiAmb.tick(0.1, 0.5);

            if ( theAmbData.myReceivedInitiateFederateSave )
            {
               // Perform a save operation
               saveFederation(rtiAmb, theAmbData.mySaveLabel, false);
            }

            if ( theAmbData.myReceivedFederationRestoreBegun )
            {
               // Perform a restore operation
               restoreFederation(rtiAmb, theAmbData.myRestoreLabel, false);
            }
         }

         int key = input.keybrdTick();
         if (key  < 0)
         {
            break;
         }
         else if ('c' == key || 'C' == key )
         {
            doConnect = !connected;
         }
         else if ('d' == key || 'D' == key )
         {
            if (connected)
            {
               try
               {
                  resignAndDestroy(rtiAmb, federationName);

                  if (attrValues)
                  {
                     delete attrValues;
                     attrValues = 0;
                  }
                  if (paramValues)
                  {
                     delete paramValues;
                     paramValues = 0;
                  }
                  connected = false;
               }
               catch(RTI::Exception& ex)
               {
                  cout << "RTI Exception: "
                     << ex._name << " " << ex._reason << endl;
               }
            }
         }
         else if ('s' == key || 'S' == key )
         {
            if (connected)
            {
               // Initiate a save operation
               const std::string saveLabel("rtiSimpleSave");
               if (!saveFederation(rtiAmb, saveLabel, true))
               {
                  // Save failed
                  break;
               }
            }
         }
         else if ('r' == key || 'R' == key )
         {
            if (connected)
            {
               // Initiate a restore operation
               const std::string savedLabel("rtiSimpleSave");
               if (!restoreFederation(rtiAmb, savedLabel, true))
               {
                  // Restore failed
                  break;
               }
            }
         }
         
#ifdef WIN32
         Sleep(2000);
#else
         sleep(2);
#endif
      }

      // Resign and destroy federation
      resignAndDestroy(rtiAmb, federationName);
   }
   catch (RTI::Exception& ex)
   {
      cout  << "RTI Exception (main loop): "
            << ex._name << " "
            << ex._reason << endl;
   }
#ifdef WIN32
   WSACleanup();
#endif

   return 0;

}

