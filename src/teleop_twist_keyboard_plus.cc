#include "teleop_twist_keyboard_plus.hh"
#include <ros/package.h>
#include <termios.h>
#include <unistd.h>
#include <fcntl.h>
#include <iostream>
#include <fstream>
#include <yaml-cpp/yaml.h>
#include <signal.h>
#include <std_msgs/String.h>
#include <std_msgs/Bool.h>
#include <std_msgs/Int32.h>
#include <std_msgs/Float32.h>
#include <std_msgs/Empty.h>
#include <assert.h>

static struct termios cooked;
static int kfd = 0;

// Function to reset the terminal settings
void static resetTerminal()
{
    tcsetattr(kfd, TCSANOW, &cooked);
}

// Signal handler for graceful shutdown
void static signalHandler(int sig)
{
    resetTerminal();
    ros::shutdown();
    exit(0);
}

TeleopTwistKeyboardPlus::TeleopTwistKeyboardPlus(ros::NodeHandle &nh, ros::NodeHandle &pnh)
    : _nh(nh), _speed(0.5), _turn(1.0), _speed_limit(1000), _turn_limit(1000), _key_timeout(0.5)
{
    try
    {
        pnh.param("speed", _speed, _speed);
        pnh.param("turn", _turn, _turn);
        pnh.param("speed_limit", _speed_limit, _speed_limit);
        pnh.param("turn_limit", _turn_limit, _turn_limit);
        pnh.param("key_timeout", _key_timeout, _key_timeout);

        _cmd_vel_pub = _nh.advertise<geometry_msgs::Twist>("cmd_vel", 1);

        // Construct the full path using the package path
        std::string package_path = ros::package::getPath("teleop_twist_keyboard_plus");
        std::string config_path = package_path + "/" + "config/bindings.yaml";

        pnh.param("config_file", config_path, config_path);
        _loadBindings(config_path);
    }
    catch (const ros::Exception &e)
    {
        ROS_ERROR("Failed to initialize teleop_twist_keyboard_plus: %s", e.what());
        ros::shutdown();
        exit(1);
    }
}

TeleopTwistKeyboardPlus::~TeleopTwistKeyboardPlus()
{
}

void TeleopTwistKeyboardPlus::_loadBindings(const std::string &config_file)
{
    try
    {
        YAML::Node config = YAML::LoadFile(config_file);

        for (const auto &item : config["move_bindings"])
        {
            char key = item.second.as<std::string>()[0];
            std::string action = item.first.as<std::string>();
            _actionBindings[key] = action;

            if (action == "forward_left")
            {
                _moveBindings[key] = {1, 0, 0, 1}; // Forward-Left
            }
            else if (action == "forward")
            {
                _moveBindings[key] = {1, 0, 0, 0}; // Forward
            }
            else if (action == "forward_right")
            {
                _moveBindings[key] = {1, 0, 0, -1}; // Forward-Right
            }
            else if (action == "left")
            {
                _moveBindings[key] = {0, 0, 0, 1}; // Left
            }
            else if (action == "no_movement")
            {
                // No movement, do nothing
            }
            else if (action == "right")
            {
                _moveBindings[key] = {0, 0, 0, -1}; // Right
            }
            else if (action == "backward_left")
            {
                _moveBindings[key] = {-1, 0, 0, 1}; // Backward-Left
            }
            else if (action == "backward")
            {
                _moveBindings[key] = {-1, 0, 0, 0}; // Backward
            }
            else if (action == "backward_right")
            {
                _moveBindings[key] = {-1, 0, 0, -1}; // Backward-Right
            }
            else if (action == "up")
            {
                _moveBindings[key] = {0, 0, 1, 0}; // Up
            }
            else if (action == "down")
            {
                _moveBindings[key] = {0, 0, -1, 0}; // Down
            }
        }

        for (const auto &item : config["holonomic_move_bindings"])
        {
            char key = item.second.as<std::string>()[0];
            std::string action = item.first.as<std::string>();
            _holonomicActionBindings[key] = action;

            if (action == "holonomic_forward")
            {
                _holonomicMoveBindings[key] = {1, 0, 0, 0}; // Holonomic Forward
            }
            else if (action == "holonomic_backward")
            {
                _holonomicMoveBindings[key] = {-1, 0, 0, 0}; // Holonomic Backward
            }
            else if (action == "holonomic_left")
            {
                _holonomicMoveBindings[key] = {0, 1, 0, 0}; // Holonomic Left
            }
            else if (action == "holonomic_no_movement")
            {
                // No movement, do nothing
            }
            else if (action == "holonomic_right")
            {
                _holonomicMoveBindings[key] = {0, -1, 0, 0}; // Holonomic Right
            }
            else if (action == "holonomic_forward_left")
            {
                _holonomicMoveBindings[key] = {1, 1, 0, 0}; // Holonomic Forward-Left
            }
            else if (action == "holonomic_forward_right")
            {
                _holonomicMoveBindings[key] = {1, -1, 0, 0}; // Holonomic Forward-Right
            }
            else if (action == "holonomic_backward_left")
            {
                _holonomicMoveBindings[key] = {-1, 1, 0, 0}; // Holonomic Backward-Left
            }
            else if (action == "holonomic_backward_right")
            {
                _holonomicMoveBindings[key] = {-1, -1, 0, 0}; // Holonomic Backward-Right
            }
        }

        for (const auto &item : config["speed_bindings"])
        {
            char key = item.second.as<std::string>()[0];
            std::string action = item.first.as<std::string>();

            if (action == "increase_max_speed_by_10")
            {
                _speedBindings[key] = {1.1, 1.1};
            }
            else if (action == "decrease_max_speed_by_10")
            {
                _speedBindings[key] = {0.9, 0.9};
            }
            else if (action == "decrease_linear_speed_by_10")
            {
                _speedBindings[key] = {0.9, 1.0};
            }
            else if (action == "increase_linear_speed_by_10")
            {
                _speedBindings[key] = {1.1, 1.0};
            }
            else if (action == "decrease_angular_speed_by_10")
            {
                _speedBindings[key] = {1.0, 0.9};
            }
            else if (action == "increase_angular_speed_by_10")
            {
                _speedBindings[key] = {1.0, 1.1};
            }
        }

        for (const auto &custom_item : config["custom_bindings"])
        {
            std::string custom_key = custom_item.first.as<std::string>();
            std::string description = custom_item.second["description"].as<std::string>();
            char key = custom_item.second["key"].as<std::string>()[0];
            std::string topic = custom_item.second["topic"].as<std::string>();
            std::string topic_type = custom_item.second["topic_type"].as<std::string>();

            _custom_item_description.push_back(description);

            if (topic_type == "std_msgs/Bool")
            {
                bool data = custom_item.second["data"].as<bool>();
                auto pub = _nh.advertise<std_msgs::Bool>(topic, 1);
                _customBindings[key] = [pub, topic, data]()
                {
                    std_msgs::Bool msg;
                    msg.data = data;
                    pub.publish(msg);
                };
            }
            else if (topic_type == "std_msgs/String")
            {
                std::string data = custom_item.second["data"].as<std::string>();
                auto pub = _nh.advertise<std_msgs::String>(topic, 1);
                _customBindings[key] = [pub, topic, data]()
                {
                    std_msgs::String msg;
                    msg.data = data;
                    pub.publish(msg);
                };
            }
            else if (topic_type == "std_msgs/Int32")
            {
                int data = custom_item.second["data"].as<int>();
                auto pub = _nh.advertise<std_msgs::Int32>(topic, 1);
                _customBindings[key] = [pub, topic, data]()
                {
                    std_msgs::Int32 msg;
                    msg.data = data;
                    pub.publish(msg);
                };
            }
            else if (topic_type == "std_msgs/Float32")
            {
                float data = custom_item.second["data"].as<float>();
                auto pub = _nh.advertise<std_msgs::Float32>(topic, 1);
                _customBindings[key] = [pub, topic, data]()
                {
                    std_msgs::Float32 msg;
                    msg.data = data;
                    pub.publish(msg);
                };
            }
            else if (topic_type == "std_msgs/Empty")
            {
                auto pub = _nh.advertise<std_msgs::Empty>(topic, 1);
                _customBindings[key] = [pub, topic]()
                {
                    std_msgs::Empty msg;
                    pub.publish(msg);
                };
            }
            else
            {
                ROS_ERROR("Unknown topic type %s for custom binding %s", topic_type.c_str(), custom_key.c_str());
            }
        }
    }
    catch (const YAML::Exception &e)
    {
        ROS_ERROR("Failed to load bindings from %s: %s", config_file.c_str(), e.what());
        ros::shutdown();
        exit(1);
    }
}


void TeleopTwistKeyboardPlus::_printHelpMessage()
{
    std::cout << "Reading from the keyboard and publishing to Twist!\n";
    std::cout << "---------------------------\n";
    std::cout << "Moving around:\n";

    // Helper lambda to find and print a key for a given action
    auto findAndPrintKey = [](const std::unordered_map<char, std::string> &bindings, const std::string &action)
    {
        for (const auto &binding : bindings)
        {
            if (binding.second == action)
            {
                std::cout << binding.first << "    ";
                return;
            }
        }
        std::cout << "     ";
    };

    // Print the standard movement keys in the required format
    findAndPrintKey(_actionBindings, "forward_left");
    findAndPrintKey(_actionBindings, "forward");
    findAndPrintKey(_actionBindings, "forward_right");
    std::cout << "\n";
    findAndPrintKey(_actionBindings, "left");
    findAndPrintKey(_actionBindings, "no_movement");
    findAndPrintKey(_actionBindings, "right");
    std::cout << "\n";
    findAndPrintKey(_actionBindings, "backward_left");
    findAndPrintKey(_actionBindings, "backward");
    findAndPrintKey(_actionBindings, "backward_right");
    std::cout << "\n";

    std::cout << "\nFor Holonomic mode (strafing), hold down the shift key:\n";
    std::cout << "---------------------------\n";

    // Print the holonomic movement keys in the required format
    findAndPrintKey(_holonomicActionBindings, "holonomic_forward_left");
    findAndPrintKey(_holonomicActionBindings, "holonomic_forward");
    findAndPrintKey(_holonomicActionBindings, "holonomic_forward_right");
    std::cout << "\n";
    findAndPrintKey(_holonomicActionBindings, "holonomic_left");
    findAndPrintKey(_holonomicActionBindings, "holonomic_no_movement");
    findAndPrintKey(_holonomicActionBindings, "holonomic_right");
    std::cout << "\n";
    findAndPrintKey(_holonomicActionBindings, "holonomic_backward_left");
    findAndPrintKey(_holonomicActionBindings, "holonomic_backward");
    findAndPrintKey(_holonomicActionBindings, "holonomic_backward_right");
    std::cout << "\n";

    std::cout << "\n";

    // Print the vertical movement keys
    findAndPrintKey(_actionBindings, "up");
    std::cout << " : up (+z)\n";
    findAndPrintKey(_actionBindings, "down");
    std::cout << " : down (-z)\n\n";

    std::cout << "anything else : stop\n\n";

    std::cout << "Speed:\n";
    std::cout << "---------------------------\n";
    uint8_t format_counter = 0;

    for (auto it = _speedBindings.cbegin(); it != _speedBindings.cend(); ++it) 
    {
            switch(format_counter)
            {
                case 0:
                    std::cout << (*it).first << " : increase max speeds by 10%\n";
                    format_counter++;
                    break;
                case 1:
                    std::cout << (*it).first << " : decrease max speeds by 10%\n";
                    format_counter++;
                    break;
                case 2:
                    std::cout << (*it).first << " : increase linear speed by 10%\n";
                    format_counter++;
                    break;
                case 3:
                    std::cout << (*it).first << " : decrease linear speed by 10%\n";
                    format_counter++;
                    break;
                case 4:
                    std::cout << (*it).first << " : increase angular speed by 10%\n";
                    format_counter++;
                    break;
                case 5:
                    std::cout << (*it).first << " : decrease angular speed by 10%\n";
                    format_counter = 0;
                    break;
                default:
                    break;
            }
    }

    std::cout << "\nCTRL-C to quit" << std::endl;

    // Print the custom bindings
    std::cout << "\nCustom bindings:\n";
    std::cout << "---------------------------\n";

    // Print custom key and description
    for (size_t i = 0; i < _custom_item_description.size(); ++i)
    {
        std::cout << _custom_item_description[i] << " : " << (i + 1) << "\n";

    }

    std::cout << std::endl;
}

void TeleopTwistKeyboardPlus::keyLoop()
{
    char c;
    kfd = 0;

    // get the console in raw mode
    tcgetattr(kfd, &cooked);
    struct termios raw;
    memcpy(&raw, &cooked, sizeof(struct termios));
    raw.c_lflag &= ~(ICANON | ECHO);
    raw.c_cc[VEOL] = 1;
    raw.c_cc[VEOF] = 2;
    tcsetattr(kfd, TCSANOW, &raw);

    // Register signal handler for CTRL-C
    signal(SIGINT, signalHandler);

    _printHelpMessage();

    while (ros::ok())
    {
        // get the next event from the keyboard
        if (read(kfd, &c, 1) < 0)
        {
            perror("read():");
            resetTerminal();
            exit(-1);
        }

        if (_moveBindings.find(c) != _moveBindings.end())
        {
            _twist_msg.linear.x = _moveBindings[c][0] * _speed;
            _twist_msg.linear.y = _moveBindings[c][1] * _speed;
            _twist_msg.linear.z = _moveBindings[c][2] * _speed;
            _twist_msg.angular.x = 0;
            _twist_msg.angular.y = 0;
            _twist_msg.angular.z = _moveBindings[c][3] * _turn;
        }
        else if (_holonomicMoveBindings.find(c) != _holonomicMoveBindings.end())
        {
            _twist_msg.linear.x = _holonomicMoveBindings[c][0] * _speed;
            _twist_msg.linear.y = _holonomicMoveBindings[c][1] * _speed;
            _twist_msg.linear.z = _holonomicMoveBindings[c][2] * _speed;
            _twist_msg.angular.x = 0;
            _twist_msg.angular.y = 0;
            _twist_msg.angular.z = _holonomicMoveBindings[c][3] * _turn;
        }
        else if (_speedBindings.find(c) != _speedBindings.end())
        {
            _speed = std::min(_speed_limit, _speed * _speedBindings[c].first);
            _turn = std::min(_turn_limit, _turn * _speedBindings[c].second);
            std::cout << "Speed set to: " << _speed << ", Turn set to: " << _turn << std::endl;
        }
        else if (_customBindings.find(c) != _customBindings.end())
        {
            _customBindings[c]();
        }
        else
        {
            // Skip updating cmd_vel if key timeout and robot already stopped
            if (c == 0 && _twist_msg.linear.x == 0 && _twist_msg.linear.y == 0 &&
                _twist_msg.linear.z == 0 && _twist_msg.angular.z == 0)
            {
                continue;
            }
            _twist_msg.linear.x = 0;
            _twist_msg.linear.y = 0;
            _twist_msg.linear.z = 0;
            _twist_msg.angular.x = 0;
            _twist_msg.angular.y = 0;
            _twist_msg.angular.z = 0;
            if (c == '\x03')
            {
                break;
            }
        }

        _cmd_vel_pub.publish(_twist_msg);
    }

    resetTerminal();
}
