/*
 * Copyright (c) 2023 IPADS, Shanghai Jiao Tong University.
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef DDSSERVER_H_
#define DDSSERVER_H_

#include "ClientServerTypes.h"

#include <fastdds/dds/domain/DomainParticipant.hpp>
#include <fastdds/dds/publisher/DataWriter.hpp>
#include <fastdds/dds/publisher/DataWriterListener.hpp>
#include <fastdds/dds/publisher/Publisher.hpp>
#include <fastdds/dds/subscriber/DataReader.hpp>
#include <fastdds/dds/subscriber/DataReaderListener.hpp>
#include <fastdds/dds/subscriber/SampleInfo.hpp>
#include <fastdds/dds/subscriber/Subscriber.hpp>
#include <fastdds/dds/topic/Topic.hpp>
#include <unordered_map>
#include <vector>
#include "Serialization.h"
#include "Util.h"
#include "fastdds/dds/core/status/SubscriptionMatchedStatus.hpp"

#define CALL_SERVER_TIMEOUT 1000
#define HISTORY_DEPTH 10000
#define MAX_SAMPLES 150000
#define ALLOC_SAMPLES 100000
#define TIMEOUT_THOUSAND 1000
#define TIMEOUT_HUNDRED 100
#define RETRY_COUNT 10

#define ONE 1
#define TWO 2
#define THREE 3

class SoftbusServer;

class DDSServer {
    friend class OperationListener;
    friend class ResultListener;

public:
    DDSServer();

    virtual ~DDSServer();

    void init()
    {
    }

    bool publish_service(std::string service_name);

    // Serve indefinitely.
    void serve();

    void serve(class SoftbusServer *_server);

    std::string m_guid;
    uint32_t m_n_served;

    class OperationListener
        : public eprosima::fastdds::dds::DataReaderListener {
    public:
        class SoftbusServer *server;

        OperationListener(DDSServer *up) : mp_up(up)
        {
        }

        ~OperationListener() override
        {
        }

        DDSServer *mp_up;

        void
        on_data_available(eprosima::fastdds::dds::DataReader *reader) override;

        clientserver::Operation m_operation;

        clientserver::Result m_result;
    };

    class ResultListener : public eprosima::fastdds::dds::DataWriterListener {
    public:
        ResultListener(DDSServer *up) : mp_up(up)
        {
        }

        ~ResultListener() override
        {
        }

        DDSServer *mp_up;
    };

    OperationListener m_operationsListener;
    ResultListener m_resultsListener;

    bool m_isReadyDetect = false;
    int m_operationMatchedDetect = 0;
    int m_resultMatchedDetect = 0;

    bool isReadyDetect()
    {
        if (m_operationMatchedDetect >= 1 && m_resultMatchedDetect >= 1) {
            m_isReadyDetect = true;
        } else {
            m_isReadyDetect = false;
        }
        return m_isReadyDetect;
    }

    class OperationDetectListener
        : public eprosima::fastdds::dds::DataWriterListener {
    public:
        OperationDetectListener(DDSServer *up) : mp_up(up)
        {
        }

        ~OperationDetectListener() override
        {
        }

        DDSServer *mp_up;

        void on_publication_matched(
            eprosima::fastdds::dds::DataWriter * /* writer */,
            const eprosima::fastdds::dds::PublicationMatchedStatus &info)
            override
        {
            std::cout << "[DDSSERVER] PUBLICATION MATCHED" << std::endl;
            if (info.current_count_change == 1) {
                mp_up->m_operationMatchedDetect++;
            } else if (info.current_count_change == -1) {
                mp_up->m_resultMatchedDetect--;
            } else {
                std::cout << info.current_count_change
                          << " is not a valid value for "
                             "PublicationMatchedStatus current count change"
                          << std::endl;
            }
            mp_up->isReadyDetect();
        }
    };

    class ResultDetectListener
        : public eprosima::fastdds::dds::DataReaderListener {
    public:
        ResultDetectListener(DDSServer *up) : mp_up(up)
        {
        }

        ~ResultDetectListener() override
        {
        }

        DDSServer *mp_up;

        void on_data_available(
            eprosima::fastdds::dds::DataReader * /* reader */) override
        {
        }

        void on_subscription_matched(
            eprosima::fastdds::dds::DataReader * /* reader */,
            const eprosima::fastdds::dds::SubscriptionMatchedStatus &info)
            override
        {
            std::cout << "[DDSSERVER] SUBSCRIPTION MATCHED" << std::endl;
            if (info.current_count_change == 1) {
                mp_up->m_resultMatchedDetect++;
            } else if (info.current_count_change == -1) {
                mp_up->m_resultMatchedDetect--;
            } else {
                std::cout << info.current_count_change
                          << " is not a valid value for "
                             "SubscriptionMatchedStatus current count change"
                          << std::endl;
            }
            mp_up->isReadyDetect();
        }
    };

    OperationDetectListener m_operationsDetectListener;
    ResultDetectListener m_resultsDetectListener;

private:
    eprosima::fastdds::dds::Subscriber *mp_operation_sub;

    eprosima::fastdds::dds::DataReader *mp_operation_reader;

    eprosima::fastdds::dds::Publisher *mp_result_pub;

    eprosima::fastdds::dds::DataWriter *mp_result_writer;

    eprosima::fastdds::dds::Topic *mp_operation_topic;

    eprosima::fastdds::dds::Topic *mp_result_topic;

    eprosima::fastdds::dds::DomainParticipant *mp_participant;

    eprosima::fastdds::dds::TypeSupport mp_resultdatatype;

    eprosima::fastdds::dds::TypeSupport mp_operationdatatype;

    eprosima::fastdds::dds::TypeSupport mp_resultdatatype_detect;

    eprosima::fastdds::dds::TypeSupport mp_operationdatatype_detect;

    bool detect_ribbon(std::string service_name);
    bool create_ribbon(std::string service_name);
    void create_participant(std::string pqos_name);
    void sendMessageToRibbon(eprosima::fastdds::dds::DataWriter *writer_detect,
                             eprosima::fastdds::dds::DataReader *reader_detect);
};

// use round-robin to call the server
class DDSRouter {
    friend class OperationListener;
    friend class ResultListener;

public:
    std::string service_name;
    static int call_times;
    int next_enclave_id = 1;
    static std::unordered_map<int, int> enclave_id_to_server_index;

    DDSRouter(std::string _service_name);
    virtual ~DDSRouter();
    bool init();

    bool add_server(std::string server_guid);
    bool call_server(clientserver::Operation &client_op,
                     std::vector<char> &result);

    class OperationListener
        : public eprosima::fastdds::dds::DataReaderListener {
    public:
        OperationListener(DDSRouter *up) : mp_up(up)
        {
        }

        ~OperationListener() override
        {
        }

        DDSRouter *mp_up;

        void on_subscription_matched(
            eprosima::fastdds::dds::DataReader * /* writer */,
            const eprosima::fastdds::dds::SubscriptionMatchedStatus
                &info /* info */) override
        {

            if (info.current_count_change == 1) {
                std::cout << "DDSRouter SUBSCRIPTION MATCHED" << std::endl;
            } else if (info.current_count_change == -1) {
                std::cout << "DDSRouter SUBSCRIPTION UNMATCHED" << std::endl;
            }
        }

        void
        on_data_available(eprosima::fastdds::dds::DataReader * /* reader */)
        {
            mp_up->call_times++;
            eprosima::fastdds::dds::SampleInfo m_sampleInfo;
            clientserver::Operation m_operation;
            clientserver::Result m_result;
            auto ret = mp_up->mp_operation_reader->take_next_sample(
                (char *)&m_operation, &m_sampleInfo);

            if (m_sampleInfo.valid_data) {
                m_result.m_guid = m_operation.m_guid;
                int operation_type = m_operation.m_type;
                if (operation_type == NOTIFICATION_MESSAGE) {
                    printf("DDSRouter Received NOTIFICATION_MESSAGE\n");
                    std::vector<char> register_guid_vector =
                        m_operation.m_vector;
                    std::string temp_guid;
                    temp_guid.insert(temp_guid.begin(),
                                     register_guid_vector.begin(),
                                     register_guid_vector.end());
                    mp_up->add_server(temp_guid);
                    m_result.m_type = NOTIFICATION_MESSAGE;
                    mp_up->mp_result_writer->write((char *)&m_result);
                } else if (operation_type == NORMAL_MESSAGE) {
                    std::vector<char> result_vector;
                    m_result.m_type = DUMMY_MESSAGE;
                    mp_up->mp_result_writer->write((char *)&m_result);

                    mp_up->call_server(m_operation, result_vector);
                    // mp_up->call_server(m_operation.m_vector, result_vector,
                    //                    m_operation.m_enclave_id);
                    m_result.m_type = NORMAL_MESSAGE;
                    m_result.m_vector = result_vector;
                    m_result.m_vector_size = result_vector.size();
                    m_result.m_enclave_id = m_operation.m_enclave_id;
                    m_result.m_guid = m_operation.m_guid;
                    m_result.ack_idx = m_operation.fragment_idx;
                    mp_up->mp_result_writer->write((char *)&m_result);
                } else {
                    m_result.m_guid = m_operation.m_guid;
                    m_result.m_type = DUMMY_MESSAGE;
                    mp_up->mp_result_writer->write((char *)&m_result);
                }
            }
        }
    };

    class ResultListener : public eprosima::fastdds::dds::DataWriterListener {
    public:
        ResultListener(DDSRouter *up) : mp_up(up)
        {
        }

        ~ResultListener() override
        {
        }

        DDSRouter *mp_up;
    };

    OperationListener m_operationsListener;
    ResultListener m_resultsListener;

    class OperationServerListener
        : public eprosima::fastdds::dds::DataWriterListener {
    public:
        OperationServerListener(DDSRouter *up) : mp_up(up)
        {
        }

        ~OperationServerListener() override
        {
        }

        DDSRouter *mp_up;

        void on_publication_matched(
            eprosima::fastdds::dds::DataWriter * /* writer */,
            const eprosima::fastdds::dds::PublicationMatchedStatus & /* info */)
        {
        }
    };

    class ResultServerListener
        : public eprosima::fastdds::dds::DataReaderListener {
    public:
        ResultServerListener(DDSRouter *up) : mp_up(up)
        {
        }

        ~ResultServerListener() override
        {
        }

        DDSRouter *mp_up;

        void
        on_data_available(eprosima::fastdds::dds::DataReader * /* reader */)
        {
        }

        void on_subscription_matched(
            eprosima::fastdds::dds::DataReader * /* reader */,
            const eprosima::fastdds::dds::SubscriptionMatchedStatus
                & /* info */)
        {
        }
    };

    std::vector<OperationServerListener *> m_operationsServerListenerList;
    std::vector<ResultServerListener *> m_resultsServerListenerList;

private:
    eprosima::fastdds::dds::Subscriber *mp_operation_sub;
    eprosima::fastdds::dds::DataReader *mp_operation_reader;
    eprosima::fastdds::dds::Publisher *mp_result_pub;
    eprosima::fastdds::dds::DataWriter *mp_result_writer;
    eprosima::fastdds::dds::Topic *mp_operation_topic;
    eprosima::fastdds::dds::Topic *mp_result_topic;
    eprosima::fastdds::dds::DomainParticipant *mp_participant;
    eprosima::fastdds::dds::TypeSupport mp_resultdatatype;
    eprosima::fastdds::dds::TypeSupport mp_operationdatatype;

    // The following parameters are used to save some information of the real
    // server
    std::vector<eprosima::fastdds::dds::Topic *> mp_operation_topic_server_list;
    std::vector<eprosima::fastdds::dds::Topic *> mp_result_topic_server_list;
    std::vector<eprosima::fastdds::dds::DataReader *>
        mp_result_reader_server_list;
    std::vector<eprosima::fastdds::dds::DataWriter *>
        mp_operation_writer_server_list;
    std::vector<int> server_status_list; // 1:running; 2:busy; 3:closed
    std::vector<std::string> guid_server_list;
    int index = 0; // The next index of server to be called
    int server_num = 0;

    void create_participant(std::string pqos_name);
    bool first_add_server(std::string server_guid,
                          eprosima::fastdds::dds::DataReader *&result_reader,
                          eprosima::fastdds::dds::DataWriter *&operation_writer,
                          eprosima::fastdds::dds::Topic *&operation_topic,
                          eprosima::fastdds::dds::Topic *&result_topic);
    bool
    non_first_add_server(std::string server_guid,
                         eprosima::fastdds::dds::DataReader *&result_reader,
                         eprosima::fastdds::dds::DataWriter *&operation_writer,
                         eprosima::fastdds::dds::Topic *&operation_topic,
                         eprosima::fastdds::dds::Topic *&result_topic);
    void adjust_index();
};

#endif /* DDSSERVER_H_ */
