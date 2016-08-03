#ifndef RSCONTROLEDAEMANAGER_H
#define RSCONTROLEDAEMANAGER_H

#include <rs/utils/RSAnalysisEngineManager.h>

#include <rs_kbreasoning/DesignatorWrapper.h>
#include <rs_kbreasoning/RSControledAnalysisEngine.h>
#include <rs_kbreasoning/KRDefinitions.h>
#include <rs_kbreasoning/JsonPrologInterface.h>


#include <designator_integration_msgs/DesignatorCommunication.h>
#include <iai_robosherlock_msgs/SetRSContext.h>
#include <iai_robosherlock_msgs/RSQueryService.h>

#include <semrec_client/BeliefstateClient.h>
#include <semrec_client/Context.h>

#include <rapidjson/rapidjson.h>
#include <rapidjson/document.h>

class RSControledAEManager
  //  public RSAnalysisEngineManager<RSControledAnalysisEngine>
{

private:

  RSControledAnalysisEngine engine;
  JsonPrologInterface jsonPrologInterface_;

  ros::NodeHandle nh_;
  ros::Publisher desig_pub_;
  ros::ServiceServer service, singleService, setContextService, jsonService;

  const bool waitForServiceCall_;
  const bool useVisualizer_;
  const bool useCWAssumption_;

  std::mutex processing_mutex_;

  rs::Visualizer visualizer_;

  semrec_client::BeliefstateClient *semrecClient;
  semrec_client::Context *ctxMain;

  std::string configFile;
  std::vector<std::string> closedWorldAssumption;

public:
  RSControledAEManager(const bool useVisualizer, const std::string &savePath,
                       const bool &waitForServiceCall, const bool useCWAssumption, ros::NodeHandle n):
    jsonPrologInterface_(), nh_(n), waitForServiceCall_(waitForServiceCall),
    useVisualizer_(useVisualizer),useCWAssumption_(useCWAssumption), visualizer_(savePath)
  {

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

    desig_pub_ = nh_.advertise<designator_integration_msgs::DesignatorResponse>(std::string("result_advertiser"), 5);

    service = n.advertiseService("designator_request/all_solutions",
                                 &RSControledAEManager::designatorAllSolutionsCallback, this);

    // Call this service, if RoboSherlock should try out only
    // the pipeline with all Annotators, that provide the requested types (for example shape)
    singleService = n.advertiseService("designator_request/single_solution",
                                       &RSControledAEManager::designatorSingleSolutionCallback, this);

    // Call this service to switch between AEs
    setContextService = n.advertiseService("set_context", &RSControledAEManager::resetAECallback, this);

    jsonService = n.advertiseService("json_query", &RSControledAEManager::jsonQueryCallback, this);

    semrecClient=NULL;
    ctxMain=NULL;
  }
  ~RSControledAEManager()
  {
    delete semrecClient;
    delete ctxMain;
    uima::ResourceManager::deleteInstance();
    outInfo("RSControledAnalysisEngine Stoped");
  }

  /*brief
   * init the AE Manager
   **/
  void init(std::string &xmlFile,std::string configFile)
  {
    this->configFile = configFile;
    cv::FileStorage fs(configFile, cv::FileStorage::READ);
    fs["cw_assumption"] >>closedWorldAssumption;
    engine.init(xmlFile,configFile);
    outInfo("Number of objects in closed world assumption: "<<closedWorldAssumption.size());
    if(!closedWorldAssumption.empty() && useCWAssumption_)
    {

      for(auto cwa:closedWorldAssumption)
      {
        outInfo(cwa);
      }
      engine.setCWAssumption(closedWorldAssumption);
    }
    if(useVisualizer_)
    {
      visualizer_.start();
    }
  }

  /* brief
   * run the AE in the manager
   */
  void run();

  void stop()
  {
    if(useVisualizer_)
    {
      visualizer_.stop();
    }
    engine.resetCas();
    engine.stop();
  }

  bool resetAECallback(iai_robosherlock_msgs::SetRSContext::Request &req,
                       iai_robosherlock_msgs::SetRSContext::Response &res);

  bool designatorAllSolutionsCallback(designator_integration_msgs::DesignatorCommunication::Request &req,
                                      designator_integration_msgs::DesignatorCommunication::Response &res);

  bool designatorSingleSolutionCallback(designator_integration_msgs::DesignatorCommunication::Request &req,
                                        designator_integration_msgs::DesignatorCommunication::Response &res);

  bool designatorCallbackLogic(designator_integration_msgs::DesignatorCommunication::Request &req,
                               designator_integration_msgs::DesignatorCommunication::Response &res, bool allSolutions);

  bool jsonQueryCallback(iai_robosherlock_msgs::RSQueryService::Request &req,
                         iai_robosherlock_msgs::RSQueryService::Response &res);

  //TODO: move to Designator wrapper or somewhere else
  void filterResults(designator_integration::Designator &requestDesignator,
                     const std::vector<designator_integration::Designator> &resultDesignators,
                     std::vector<designator_integration::Designator> &filteredResponse,
                     std::string superclass);

};

#endif // RSCONTROLEDAEMANAGER_H
