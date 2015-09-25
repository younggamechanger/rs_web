/*------------------------------------------------------------------------

 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied.  See the License for the
 * specific language governing permissions and limitations
 * under the License.

 --------------------------------------------------------------------------

 Test driver that reads text files or XCASs or XMIs and calls the annotator

 -------------------------------------------------------------------------- */

/* ----------------------------------------------------------------------- */
/*       Include dependencies                                              */
/* ----------------------------------------------------------------------- */

#include <stdio.h>
#include <errno.h>
#include <sys/stat.h>
#include <thread>
#include <mutex>
#include <chrono>
#include <condition_variable>
#include <sstream>

#include <uima/api.hpp>
#include "uima/internal_aggregate_engine.hpp"
#include "uima/annotator_mgr.hpp"

#include <rs/utils/output.h>
#include <rs/utils/time.h>
#include <rs/utils/exception.h>
#include <rs/io/visualizer.h>
#include <rs/scene_cas.h>

#include <designator_integration_msgs/Designator.h>
#include <designator_integration_msgs/DesignatorCommunication.h>

#include <rs_kbreasoning/RSPipelineManager.h>
#include <rs_kbreasoning/DesignatorWrapper.h>

// designator_integration classes
#include <designators/Designator.h>


#include <ros/ros.h>
#include <ros/package.h>

#ifdef JSON_PROLOG_FOUND
#include <json_prolog/prolog.h>
#endif

// TODO
//  Allow the modifaction of multiple AEs

#undef OUT_LEVEL
#define OUT_LEVEL OUT_LEVEL_DEBUG

#define SEARCHPATH "/descriptors/analysis_engines/"

// This mutex will be locked, if:
//   1) The RSAnalysisEngineManager wants to execute pipelines
//   or
//   2) If a service call has come in and needs several process calls
//      on a RSAnalysisEngine to complete
std::mutex processing_mutex;

/* ----------------------------------------------------------------------- */
/*       Implementation                                                    */
/* ----------------------------------------------------------------------- */

class RSAnalysisEngine
{
  // boost::mutex mx;
  // std::mutex process_mutex;
private:
  std::string name;

  uima::AnalysisEngine *engine;
  uima::CAS *cas;

  RSPipelineManager *rspm;
  bool pipeline_ordering_to_change;
  std::vector<std::string> next_pipeline_order;

  boost::shared_ptr<std::mutex> process_mutex;


public:

  void setNextPipelineOrder(std::vector<std::string> l)
  {
    next_pipeline_order = l;
    setPipelineOrderChange(true);
  }

  std::vector<std::string> &getNextPipelineOrder()
  {
    return next_pipeline_order;
  }

  void applyNextPipelineOrder()
  {
    if(rspm)
    {
      rspm->setPipelineOrdering(next_pipeline_order);
    }
    setPipelineOrderChange(false);
  }

  RSAnalysisEngine() : engine(NULL), cas(NULL), rspm(NULL), pipeline_ordering_to_change(false)
  {
    process_mutex = boost::shared_ptr<std::mutex>(new std::mutex);
  }

  ~RSAnalysisEngine()
  {
    if(cas)
    {
      delete cas;
      cas = NULL;
    }
    if(engine)
    {
      delete engine;
      engine = NULL;
    }

    if(rspm)
    {
      delete rspm;
      rspm = NULL;
    }
  }

  bool pipelineOrderChange()
  {
    return pipeline_ordering_to_change;
  }

  void setPipelineOrderChange(bool b)
  {
    pipeline_ordering_to_change = b;
  }

  void resetPipelineOrdering()
  {
    if(rspm)
    {
      rspm->resetPipelineOrdering();
    }
  }

  bool defaultPipelineEnabled()
  {
    if(rspm)
    {
      return rspm->use_default_pipeline;
    }
    return false;
  }

  void init(const std::string &file)
  {
    uima::ErrorInfo errorInfo;

    size_t pos = file.rfind('/');
    outInfo("Creating analysis engine: " FG_BLUE << (pos == file.npos ? file : file.substr(pos)));

    engine = uima::Framework::createAnalysisEngine(file.c_str(), errorInfo);


    if(errorInfo.getErrorId() != UIMA_ERR_NONE)
    {
      outError("createAnalysisEngine failed.");
      throw uima::Exception(errorInfo);
    }
    // RSPipelineManager rspm(engine);
    rspm = new RSPipelineManager(engine);
    std::vector<icu::UnicodeString> &non_const_nodes = rspm->getFlowConstraintNodes();

    outInfo("*** Fetch the FlowConstraint nodes. Size is: "  << non_const_nodes.size());
    for(int i = 0; i < non_const_nodes.size(); i++)
    {
//      outInfo(non_const_nodes.at(i));
    }

    rspm->aengine->getNbrOfAnnotators();
    outInfo("*** Number of Annotators in AnnotatorManager: " << rspm->aengine->getNbrOfAnnotators());

    // After all annotators have been initialized, pick the default pipeline
    std::vector<std::string> default_pipeline;
    default_pipeline.push_back("CollectionReader");
    default_pipeline.push_back("ImagePreprocessor");
    default_pipeline.push_back("RegionFilter");
    default_pipeline.push_back("NormalEstimator");
    default_pipeline.push_back("PlaneAnnotator");
    default_pipeline.push_back("PointCloudClusterExtractor");
    default_pipeline.push_back("Cluster3DGeometryAnnotator");
    default_pipeline.push_back("ClusterTFLocationAnnotator");
    default_pipeline.push_back("SacModelAnnotator");
    default_pipeline.push_back("PrimitiveShapeAnnotator");
    //    default_pipeline.push_back("VisualizerAnnotator");
    default_pipeline.push_back("ShoppingResultAdvertiser");
    default_pipeline.push_back("StorageWriter");
    // default_pipeline.push_back("ClusterColorHistogramCalculator"); // removed color histogram for tests
    //
    rspm->setDefaultPipelineOrdering(default_pipeline);

    // Get a new CAS
    outInfo("Creating a new CAS");
    cas = engine->newCAS();

    if(cas == NULL)
    {
      outError("Creating new CAS failed.");
      engine->destroy();
      delete engine;
      engine = NULL;
      throw uima::Exception(uima::ErrorMessage(UIMA_ERR_ENGINE_NO_CAS), UIMA_ERR_ENGINE_NO_CAS, uima::ErrorInfo::unrecoverable);
    }

    outInfo("initialization done: " << name << std::endl
            << std::endl << FG_YELLOW << "********************************************************************************" << std::endl);
  }

  void stop()
  {
    engine->collectionProcessComplete();
    engine->destroy();

    outInfo("Analysis engine stopped: " << name);
  }

  void process()
  {
    // TODO Use ptrs to avoid unnecessary memory allocation?
    designator_integration_msgs::DesignatorResponse d;
    process(d);
  }

  void process(designator_integration_msgs::DesignatorResponse &designator_response)
  {
    outInfo("executing analisys engine: " << name);
    try
    {
      UnicodeString ustrInputText;
      ustrInputText.fromUTF8(name);
      cas->setDocumentText(uima::UnicodeStringRef(ustrInputText));

      outInfo("processing CAS");
      uima::CASIterator casIter = engine->processAndOutputNewCASes(*cas);

      for(int i = 0; casIter.hasNext(); ++i)
      {
        uima::CAS &outCas = casIter.next();

        // release CAS
        outInfo("release CAS " << i);
        engine->getAnnotatorContext().releaseCAS(outCas);
      }

    }
    catch(const rs::Exception &e)
    {
      outError("Exception: " << std::endl << e.what());
    }
    catch(const uima::Exception &e)
    {
      outError("Exception: " << std::endl << e);
    }
    catch(const std::exception &e)
    {
      outError("Exception: " << std::endl << e.what());
    }
    catch(...)
    {
      outError("Unknown exception!");
    }
    // Make a designator from the result
    rs::DesignatorWrapper dw;
    dw.setMode(rs::DesignatorWrapper::CLUSTER);
    dw.setCAS(cas);

    designator_response = dw.getDesignatorResponseMsg();
    cas->reset();
    outInfo("processing finished");
  }

  // Call process() and
  // decide if the pipeline should be reset or not
  void process(bool reset_pipeline_after_process, designator_integration_msgs::DesignatorResponse &designator_response)
  {
    process_mutex->lock();
    outInfo(FG_CYAN << "process(bool,desig) - LOCK OBTAINED");
    process(designator_response);
    if(reset_pipeline_after_process)
    {
      resetPipelineOrdering();  // reset pipeline to default
    }
    process_mutex->unlock();
    outInfo(FG_CYAN << "process(bool,desig) - LOCK RELEASED");
  }
  // Call process() and
  // decide if the pipeline should be reset or not
  void process(bool reset_pipeline_after_process)
  {
    designator_integration_msgs::DesignatorResponse d;
    process(reset_pipeline_after_process, d);
  }

  // Define a pipeline that should be executed,
  // process(reset_pipeline_after_process) everything and
  // decide if the pipeline should be reset or not
  void process(std::vector<std::string> annotators, bool reset_pipeline_after_process, designator_integration_msgs::DesignatorResponse &designator_response)
  {
    process_mutex->lock();
    outInfo(FG_CYAN << "process(std::vector, bool) - LOCK OBTAINED");
    setNextPipelineOrder(annotators);
    applyNextPipelineOrder();

    // Process the modified pipeline
    process(designator_response);
    if(reset_pipeline_after_process)
    {
      resetPipelineOrdering();  // reset pipeline to default
    }
    process_mutex->unlock();
    outInfo(FG_CYAN << "process(std::vector, bool) - LOCK RELEASED");
  }

  // Define a pipeline that should be executed,
  // process(reset_pipeline_after_process) everything and
  // decide if the pipeline should be reset or not
  void process(std::vector<std::string> annotators, bool reset_pipeline_after_process)
  {
    designator_integration_msgs::DesignatorResponse d;
    process(annotators, reset_pipeline_after_process, d);
  }
};

class RSAnalysisEngineManager
{
private:
  std::vector<RSAnalysisEngine> engines;

  const bool useVisualizer;
  const bool usePipelineGeneration;
  const bool waitForServiceCall;
  rs::Visualizer visualizer;

public:
  RSAnalysisEngineManager(const bool useVisualizer, const std::string &savePath, const bool &usePipelineGeneration, const bool &waitForServiceCall) :
    useVisualizer(useVisualizer), usePipelineGeneration(usePipelineGeneration), waitForServiceCall(waitForServiceCall), visualizer(savePath)
  {
    // Create/link up to a UIMACPP resource manager instance (singleton)
    outInfo("Creating resource manager"); // TODO: DEBUG
    uima::ResourceManager &resourceManager = uima::ResourceManager::createInstance("RoboSherlock"); // TODO: change topic?

    switch(OUT_LEVEL)
    {
    case OUT_LEVEL_NOOUT:
    case OUT_LEVEL_ERROR:
      resourceManager.setLoggingLevel(uima::LogStream::EnError);
      break;
    case OUT_LEVEL_INFO:
      resourceManager.setLoggingLevel(uima::LogStream::EnWarning);
      break;
    case OUT_LEVEL_DEBUG:
      resourceManager.setLoggingLevel(uima::LogStream::EnMessage);
      break;
    }
  }

  ~RSAnalysisEngineManager()
  {
    uima::ResourceManager::deleteInstance();
  }

  // Returns a string with the prolog query to execute, based on the informations in the designator
  std::string buildPrologQueryFromDesignator(designator_integration::Designator *desig, bool &success)
  {
    success = false;
    if(!desig)
    {
      return "NULL POINTER PASSED TO RSAnalysisEngineManager::buildPrologQueryFromDesignator";
    }

    std::string ret = "";
    if(desig->childForKey("TYPE"))
    {
      // If the designator contains a "type" key, the highlevel is looking for a specific object of Type XY.
      // Use the corresponding Prolog Rule for object pipeline generation

      ret = "build_pipeline_for_object('";
      // Fetch the accepted predicates from the Designator
      ret += desig->childForKey("TYPE")->stringValue();
      ret += "', A)";
    }
    else
    {
      ret = "build_pipeline_from_predicates([";
      std::vector<std::string> listOfAllPredicates;

      // Fetch the accepted predicates from the Designator
      if(desig->childForKey("SHAPE"))
      {
        listOfAllPredicates.push_back("shape");
      }
      if(desig->childForKey("COLOR"))
      {
        listOfAllPredicates.push_back("color");
      }
      if(desig->childForKey("SIZE"))
      {
        listOfAllPredicates.push_back("size");
      }
      if(desig->childForKey("LOCATION"))
      {
        listOfAllPredicates.push_back("location");
      }
      if(desig->childForKey("LOGO"))
      {
        listOfAllPredicates.push_back("logo");
      }
      if(desig->childForKey("TEXT"))
      {
        listOfAllPredicates.push_back("text");
      }
      if(desig->childForKey("PRODUCT"))
      {
        listOfAllPredicates.push_back("product");
      }
      if(desig->childForKey("DETECTION"))
      {
        listOfAllPredicates.push_back("detection");
        designator_integration::KeyValuePair *kvp = desig->childForKey("DETECTION");
        if(kvp->stringValue() == "PANCAKE")
        {
          listOfAllPredicates.push_back("pancakedetector");
        }

      }
      if(desig->childForKey("HANDLE"))
      {
        listOfAllPredicates.push_back("handle");
      }
      //      if(desig->childForKey("PANCAKE"))
      //      {
      //        listOfAllPredicates.push_back("detection");
      //        listOfAllPredicates.push_back("pancakedetector");
      //      }

      for(int i = 0; i < listOfAllPredicates.size(); i++)
      {
        ret += listOfAllPredicates.at(i);
        if(i < listOfAllPredicates.size() - 1)
        {
          ret += ",";
        }
      }

      ret += "], A)";
    }
    success = true;
    return ret;
  }

  // Create a vector of Annotator Names from the result of the
  // knowrob_rs library.
  // This vector can be used as input for RSAnalysisEngine::setNextPipelineOrder
  std::vector<std::string> createPipelineFromPrologResult(std::string result)
  {
    std::vector<std::string> new_pipeline;

    // Strip the braces from the result
    result.erase(result.end() - 1);
    result.erase(result.begin());

    std::vector<std::string> list_of_annotators;
    std::stringstream resultstream(result);

    std::string token;
    while(std::getline(resultstream, token, ','))
    {
      list_of_annotators.push_back(token);
      // erase leading whitespaces
      token.erase(token.begin(), std::find_if(token.begin(), token.end(), std::bind1st(std::not_equal_to<char>(), ' ')));
      outInfo("Planned Annotator " << token);

      // From the extracted tokens, remove the prefix
      std::string prefix("http://knowrob.org/kb/rs_components.owl#");
      int prefix_length = prefix.length();

      // Erase by length, to avoid string comparison
      token.erase(0, prefix_length);
      // outInfo("Annotator name sanitized: " << token );

      new_pipeline.push_back(token);
    }
    return new_pipeline;
  }


#ifdef JSON_PROLOG_FOUND
  bool designatorAllSolutionsCallback(designator_integration_msgs::DesignatorCommunication::Request &req,
                                      designator_integration_msgs::DesignatorCommunication::Response &res)
  {
    return designatorCallbackLogic(req, res, true);
  }
  bool designatorSingleSolutionCallback(designator_integration_msgs::DesignatorCommunication::Request &req,
                                        designator_integration_msgs::DesignatorCommunication::Response &res)
  {
    return designatorCallbackLogic(req, res, false);
  }

  // Should prolog execute all solutions?
  //   -> set allSolutions=true
  bool designatorCallbackLogic(designator_integration_msgs::DesignatorCommunication::Request &req,
                               designator_integration_msgs::DesignatorCommunication::Response &res, bool allSolutions)
  {
    designator_integration::Designator *desigRequest = new designator_integration::Designator(req.request.designator);


//    if(desigRequest != NULL)
//    {
//      std::list<std::string> keys = desigRequest->keys();
//      bool foundTS = false;
//      for(std::list<std::string>::iterator it = keys.begin(); it != keys.end(); ++it)
//      {
//        if(*it == "TIMESTAMP")
//        {
//          foundTS = true;
//        }
//      }
//      if(foundTS)
//      {
//        KeyValuePair *kvp = iai_rs::DesignatorWrapper::req_designator->childForKey("TIMESTAMP");
//        std::string ts = kvp->stringValue();
//        timestamp = convertToInt(ts);
//        outInfo("received timestamp:" << timestamp);
//      }
//    }

    if(desigRequest->type() != designator_integration::Designator::OBJECT)
    {
      outInfo(" ***** RECEIVED SERVICE CALL WITH UNHANDELED DESIGNATOR TYPE (everything != OBJECT) ! Aborting... ****** ");
      return false;
    }
    if(rs::DesignatorWrapper::req_designator)
    {
      delete rs::DesignatorWrapper::req_designator;
    }

    rs::DesignatorWrapper::req_designator = new designator_integration::Designator(req.request.designator);
    outInfo("Received Designator call: ");
    rs::DesignatorWrapper::req_designator->printDesignator();

    std::string prologQuery = "";
    bool plGenerationSuccess = false;
    prologQuery = buildPrologQueryFromDesignator(desigRequest, plGenerationSuccess);

    outInfo("Query Prolog with the following command: " << prologQuery);
    if(!plGenerationSuccess)
    {
      outInfo("Aborting Prolog Query... The generated Prolog Command is invalid");
      return false;
    }

    json_prolog::Prolog pl;
    json_prolog::PrologQueryProxy bdgs = pl.query(prologQuery);


    if(bdgs.begin() == bdgs.end())
    {
      outInfo("Can't find solution for pipeline planning");
      return false; // Indicate failure
    }

    int pipelineId = 0;

    designator_integration_msgs::DesignatorResponse full_designator_response;

    // Block the RSAnalysisEngineManager  - We need the engines now
    processing_mutex.lock();

    for(json_prolog::PrologQueryProxy::iterator it = bdgs.begin();
        it != bdgs.end(); it++)
    {
      json_prolog::PrologBindings bdg = *it;
      std::string prologResult = bdg["A"].toString();
      std::vector<std::string> new_pipeline_order = createPipelineFromPrologResult(bdg["A"].toString());

      //needed for saving results and returning them on a ros topic
      if(waitForServiceCall && !new_pipeline_order.empty())
      {
        new_pipeline_order.push_back("StorageWriter");
        new_pipeline_order.push_back("ShoppingResultAdvertiser");
      }
      outInfo(FG_BLUE << "Executing Pipeline #" << pipelineId);

      // First version. Change the pipeline on the first engine
      // to a fixed set
      if(engines.size() > 0)
      {
        // This designator response will hold the OBJECT designators with the detected
        // annotations for every detected object
        designator_integration_msgs::DesignatorResponse designator_response;
        outInfo("Executing pipeline generated by service call");
        engines.at(0).process(new_pipeline_order, true, designator_response);

        outInfo("Returned " << designator_response.designators.size() << " designators on this execution");

        // Add the PIPELINEID to reference the pipeline that was responsible for detecting
        // the returned object
        for(auto & designator : designator_response.designators)
        {
          // Convert the designator msg object to a normal Designator
          designator_integration::Designator d(designator);
          // Insert the current PIPELINEID
          d.setValue("PIPELINEID", pipelineId);
          full_designator_response.designators.push_back(d.serializeToMessage());
        }

        // Define an ACTION designator with the planned pipeline
        designator_integration::Designator pipeline_action;
        pipeline_action.setType(designator_integration::Designator::ACTION);
        std::list<designator_integration::KeyValuePair *> lstDescription;
        for(auto & annotatorName : new_pipeline_order)
        {
          designator_integration::KeyValuePair *oneAnno = new designator_integration::KeyValuePair();
          oneAnno->setValue(annotatorName);
          lstDescription.push_back(oneAnno);
        }
        pipeline_action.setValue("PIPELINEID", pipelineId);
        pipeline_action.setValue("ANNOTATORS", designator_integration::KeyValuePair::LIST, lstDescription);
        full_designator_response.designators.push_back(pipeline_action.serializeToMessage());
        // Delete the allocated keyvalue pairs for the annotator names
        for(auto & kvpPtr : lstDescription)
        {
          delete kvpPtr;
        }

        outInfo("Executing pipeline generated by service call: done");
      }
      else
      {
        outInfo("ERROR: No engine set up");
        return false;
      }

      if(!allSolutions)
      {
        break;  // Only take the first solution if allSolutions == false
      }

      pipelineId++;
    }

    // All engine calls have been processed. Release the Lock
    processing_mutex.unlock();

    for(auto & designator : full_designator_response.designators)
    {
      // Convert the designator msg object to a normal Designator
      designator_integration::Designator d(designator);
      d.printDesignator();
      outInfo("------------------");
      res.response.designators.push_back(d.serializeToMessage());
    }

    return true;
  }
#endif // JSON_PROLOG_FOUND


  void init(const std::vector<std::string> &files)
  {
    engines.resize(files.size());
    for(size_t i = 0; i < engines.size(); ++i)
    {
      engines[i].init(files[i]);
    }
    if(useVisualizer)
    {
      visualizer.start();
    }
  }

  void run()
  {
    for(; ros::ok();)
    {
      processing_mutex.lock();
      if(waitForServiceCall)
      {
        usleep(100000);
      }
      else
      {

        for(size_t i = 0; i < engines.size(); ++i)
        {
          if(usePipelineGeneration)
          {
            engines[i].process(true);
          }
          else if(!waitForServiceCall)
          {
            // Default behaviour. No locking required
            engines[i].process();
          }
        }
      }
      processing_mutex.unlock();
      ros::spinOnce();
    }
  }

  void stop()
  {
    if(useVisualizer)
    {
      visualizer.stop();
    }
    for(size_t i = 0; i < engines.size(); ++i)
    {
      engines[i].stop();
    }
  }
};

/* ----------------------------------------------------------------------- */
/*       Main                                                              */
/* ----------------------------------------------------------------------- */

/**
 * Error output if program is called with wrong parameter.
 */
void help()
{
  std::cout << "Usage: runAECppLoop [options] analysisEngine.xml [...]" << std::endl
            << "Options:" << std::endl
            << "  -pipelinegen  Enable pipeline generation" << std::endl
            << "  -wait If using piepline set this to wait for a service call" << std::endl
            << "  -visualizer  Enable visualization" << std::endl
            << "  -save PATH   Path for storing images" << std::endl;
}

int main(int argc, char *argv[])
{
  /* Access the command line arguments to get the name of the input text. */
  if(argc < 2)
  {
    help();
    return 1;
  }

  ros::init(argc, argv, std::string("RoboSherlock_") + getenv("USER"));

  // Has JSON PROLOG been enabled at compile time?
#ifndef JSON_PROLOG_FOUND
  outInfo(FG_BLUE << "Running without json_prolog -  No pipeline generation is available. Please make sure that json_prolog can be found on your system when you compile RoboSherlock if you want to use this feature.");
#endif

  std::vector<std::string> args;
  args.resize(argc - 1);
  for(int argI = 1; argI < argc; ++argI)
  {
    args[argI - 1] = argv[argI];
  }

  bool useVisualizer = false;
  bool usePipelineGeneration = false;
  bool waitForServiceCall = false;
  std::string savePath = getenv("HOME");

  size_t argO = 0;
  for(size_t argI = 0; argI < args.size(); ++argI)
  {
    const std::string &arg = args[argI];

    if(arg == "-visualizer")
    {
      useVisualizer = true;
    }
    else if(arg == "-pipelinegen")
    {
      usePipelineGeneration = true;
    }
    else if(arg == "-wait")
    {
      waitForServiceCall = true;
    }
    else if(arg == "-save")
    {
      if(++argI < args.size())
      {
        savePath = args[argI];
      }
      else
      {
        outError("No save path defined!");
        return -1;
      }
    }
    else
    {
      args[argO] = args[argI];
      ++argO;
    }
  }
  args.resize(argO);

  struct stat fileStat;
  if(stat(savePath.c_str(), &fileStat) || !S_ISDIR(fileStat.st_mode))
  {
    outError("Save path \"" << savePath << "\" does not exist.");
    return -1;
  }

  std::vector<std::string> analysisEngineFiles;

  //generate a vector of possible paths for the analysis engine
  std::vector<std::string> searchPaths;

  //empty path for full path given as argument
  searchPaths.push_back("");
  //add core package path
  searchPaths.push_back(ros::package::getPath("robosherlock") + std::string(SEARCHPATH));

  //look for packages dependent on core and find their full path
  std::vector<std::string> child_packages;
  ros::package::command("depends-on robosherlock", child_packages);
  for(int i = 0; i < child_packages.size(); ++i)
  {
    searchPaths.push_back(ros::package::getPath(child_packages[i]) + std::string(SEARCHPATH));
  }

  analysisEngineFiles.resize(args.size(), "");
  for(int argI = 0; argI < args.size(); ++argI)
  {
    const std::string &arg = args[argI];
    struct stat fileStat;

    for(size_t i = 0; i < searchPaths.size(); ++i)
    {
      const std::string file = searchPaths[i] + arg;
      const std::string fileXML = file + ".xml";

      if(!stat(file.c_str(), &fileStat) && S_ISREG(fileStat.st_mode))
      {
        analysisEngineFiles[argI] = file;
        break;
      }
      else if(!stat(fileXML.c_str(), &fileStat) && S_ISREG(fileStat.st_mode))
      {
        analysisEngineFiles[argI] = fileXML;
        break;
      }
    }

    if(analysisEngineFiles[argI].empty())
    {
      outError("analysis engine \"" << arg << "\" not found.");
      return -1;
    }
  }

  ros::NodeHandle n("~");
  try
  {
    RSAnalysisEngineManager manager(useVisualizer, savePath, usePipelineGeneration, waitForServiceCall);
    ros::ServiceServer service, single_service;
#ifdef JSON_PROLOG_FOUND
    if(usePipelineGeneration)
    {
      // Call this service, if RoboSherlock should try out every possible pipeline
      // that has been generated by the pipeline planning
      service = n.advertiseService("designator_request/all_solutions", &RSAnalysisEngineManager::designatorAllSolutionsCallback, &manager);
      // Call this service, if RoboSherlock should try out only
      // the pipeline with all Annotators, that provide the requested types (for example shape)
      single_service = n.advertiseService("designator_request/single_solution", &RSAnalysisEngineManager::designatorSingleSolutionCallback, &manager);
    }
#endif

    manager.init(analysisEngineFiles);
    manager.run();

    manager.stop();
  }
  catch(const rs::Exception &e)
  {
    outError("Exception: " << std::endl << e.what());
    return -1;
  }
  catch(const uima::Exception &e)
  {
    outError("Exception: " << std::endl << e);
    return -1;
  }
  catch(const std::exception &e)
  {
    outError("Exception: " << std::endl << e.what());
    return -1;
  }
  catch(...)
  {
    outError("Unknown exception!");
    return -1;
  }
  return 0;
}