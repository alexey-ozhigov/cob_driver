#include <cob_phidgets/phidgetik_ros.h>
#include <cob_phidgets/DigitalSensor.h>
#include <cob_phidgets/AnalogSensor.h>

PhidgetIKROS::PhidgetIKROS(ros::NodeHandle nh, int serial_num, std::string board_name, XmlRpc::XmlRpcValue* sensor_params, SensingMode mode)
	:PhidgetIK(mode), _nh(nh), _serial_num(serial_num)
{
	ros::NodeHandle tmpHandle("~");
	ros::NodeHandle nodeHandle(tmpHandle, board_name);
	_outputChanged.updated=false;
	_outputChanged.index=-1;
	_outputChanged.state=0;

	_pubAnalog = _nh.advertise<cob_phidgets::AnalogSensor>("analog_sensor", 1);
	_pubDigital = _nh.advertise<cob_phidgets::DigitalSensor>("digital_sensor", 1);

	_srvDigitalOut = nodeHandle.advertiseService("set_digital", &PhidgetIKROS::setDigitalOutCallback, this);
	_srvDataRate = nodeHandle.advertiseService("set_data_rate", &PhidgetIKROS::setDataRateCallback, this);
	_srvTriggerValue = nodeHandle.advertiseService("set_trigger_value", &PhidgetIKROS::setTriggerValueCallback, this);

	if(init(_serial_num) != EPHIDGET_OK)
	{
		ROS_ERROR("Error open Phidget Board on serial %d. Message: %s",_serial_num, this->getErrorDescription(this->getError()).c_str());
	}
	if(waitForAttachment(10000) != EPHIDGET_OK)
	{
		ROS_ERROR("Error waiting for Attachment. Message: %s",this->getErrorDescription(this->getError()).c_str());
	}
	readParams(sensor_params);
}

PhidgetIKROS::~PhidgetIKROS()
{
}

auto PhidgetIKROS::readParams(XmlRpc::XmlRpcValue* sensor_params) -> void
{
	for(auto& sensor : *sensor_params)
	{
		std::string name = sensor.first;
		XmlRpc::XmlRpcValue value = sensor.second;
		if(!value.hasMember("type"))
		{
			ROS_ERROR("Sensor Param '%s' has no 'type' member. Ignoring param!", name.c_str());
			continue;
		}
		if(!value.hasMember("index"))
		{
			ROS_ERROR("Sensor Param '%s' has no 'index' member. Ignoring param!", name.c_str());
			continue;
		}
		XmlRpc::XmlRpcValue value_type = value["type"];
		XmlRpc::XmlRpcValue value_index = value["index"];
		std::string type = value_type;
		int index = value_index;

		if(type == "analog")
			_indexNameMapAnalog.insert(std::make_pair(index, name));
		else if(type == "digital_in")
			_indexNameMapDigitalIn.insert(std::make_pair(index, name));
		else if(type == "digital_out")
			_indexNameMapDigitalOut.insert(std::make_pair(index, name));
		else
			ROS_ERROR("Type '%s' in sensor param '%s' is unkown", type.c_str(), name.c_str());

		if(value.hasMember("change_trigger"))
		{
			XmlRpc::XmlRpcValue value_change_trigger = value["change_trigger"];
			int change_trigger = value_change_trigger;
			ROS_WARN("Setting change trigger to %d for sensor %s with index %d ",change_trigger, name.c_str(), index);
			setSensorChangeTrigger(index, change_trigger);
		}
		if(value.hasMember("data_rate"))
		{
			XmlRpc::XmlRpcValue value_data_rate = value["data_rate"];
			int data_rate = value_data_rate;
			ROS_WARN("Setting data rate to %d for sensor %s with index %d ",data_rate, name.c_str(), index);
			setDataRate(index, data_rate);
		}
	}
	//fill up rest of maps with default values
	int count = this->getInputCount();
	for(int i = 0; i < count; i++)
	{
		_indexNameMapItr = _indexNameMapDigitalIn.find(i);
		if(_indexNameMapItr == _indexNameMapDigitalIn.end())
		{
			std::stringstream ss;
			ss << getDeviceSerialNumber() << "/" << "in/" << i;
			_indexNameMapDigitalIn.insert(std::make_pair(i, ss.str()));
		}
	}
	count = this->getOutputCount();
	for(int i = 0; i < count; i++)
	{
		_indexNameMapItr = _indexNameMapDigitalOut.find(i);
		if(_indexNameMapItr == _indexNameMapDigitalOut.end())
		{
			std::stringstream ss;
			ss << getDeviceSerialNumber() << "/" << "out/" << i;
			_indexNameMapDigitalOut.insert(std::make_pair(i, ss.str()));
		}
	}
	count = this->getSensorCount();
	for(int i = 0; i < count; i++)
	{
		_indexNameMapItr = _indexNameMapAnalog.find(i);
		if(_indexNameMapItr != _indexNameMapAnalog.end())
		{
			std::stringstream ss;
			ss << getDeviceSerialNumber() << "/" << i;
			_indexNameMapAnalog.insert(std::make_pair(i, ss.str()));
		}
	}
}

auto PhidgetIKROS::update() -> void
{
	int count = this->getInputCount();
	cob_phidgets::DigitalSensor msg_digit;
	std::vector<std::string> names;
	std::vector<signed char> states;

	//------- publish digital input states ----------//
	for(int i = 0; i < count; i++)
	{
		std::string name;
		_indexNameMapItr = _indexNameMapDigitalIn.find(i);
		if(_indexNameMapItr != _indexNameMapDigitalIn.end())
			name = (*_indexNameMapItr).second;
		names.push_back(name);
		states.push_back(this->getInputState(i));
	}
	msg_digit.header.stamp = ros::Time::now();
	msg_digit.uri = names;
	msg_digit.state = states;

	_pubDigital.publish(msg_digit);

	//------- publish digital output states ----------//
	names.clear();
	states.clear();
	count = this->getOutputCount();
	for(int i = 0; i < count; i++)
	{
		std::string name;
		_indexNameMapItr = _indexNameMapDigitalOut.find(i);
		if(_indexNameMapItr != _indexNameMapDigitalOut.end())
			name = (*_indexNameMapItr).second;
		names.push_back(name);
		states.push_back(this->getOutputState(i));
	}
	msg_digit.header.stamp = ros::Time::now();
	msg_digit.uri = names;
	msg_digit.state = states;

	_pubDigital.publish(msg_digit);


	//------- publish analog input states ----------//
	cob_phidgets::AnalogSensor msg_analog;
	names.clear();
	std::vector<short int> values;
	count = this->getSensorCount();
	for(int i = 0; i < count; i++)
	{
		std::string name;
		_indexNameMapItr = _indexNameMapAnalog.find(i);
		if(_indexNameMapItr != _indexNameMapAnalog.end())
			name = (*_indexNameMapItr).second;
		names.push_back(name);
		values.push_back(this->getSensorValue(i));
	}
	msg_analog.header.stamp = ros::Time::now();
	msg_analog.uri = names;
	msg_analog.value = values;

	_pubAnalog.publish(msg_analog);
}

auto PhidgetIKROS::inputChangeHandler(int index, int inputState) -> int
{
	ROS_DEBUG("Board %d: Digital Input %d changed to State: %d", _serial_num, index, inputState);
	cob_phidgets::DigitalSensor msg;
	std::vector<std::string> names;
	std::vector<signed char> states;

	std::string name;
	_indexNameMapItr = _indexNameMapAnalog.find(index);
	if(_indexNameMapItr != _indexNameMapAnalog.end())
		name = (*_indexNameMapItr).second;
	names.push_back(name);
	states.push_back(inputState);

	msg.header.stamp = ros::Time::now();
	msg.uri = names;
	msg.state = states;
	_pubDigital.publish(msg);

	return 0;
}

auto PhidgetIKROS::outputChangeHandler(int index, int outputState) -> int
{
	ROS_DEBUG("Board %d: Digital Output %d changed to State: %d", _serial_num, index, outputState);
	std::lock_guard<std::mutex> lock{_mutex};
	_outputChanged.updated = true;
	_outputChanged.index = index;
	_outputChanged.state = outputState;
	return 0;
}
auto PhidgetIKROS::sensorChangeHandler(int index, int sensorValue) -> int
{
	ROS_DEBUG("Board %d: Analog Input %d changed to Value: %d", _serial_num, index, sensorValue);
	cob_phidgets::AnalogSensor msg;
	std::vector<std::string> names;
	std::vector<short int> values;

	std::string name;
	_indexNameMapItr = _indexNameMapAnalog.find(index);
	if(_indexNameMapItr != _indexNameMapAnalog.end())
		name = (*_indexNameMapItr).second;
	names.push_back(name);
	values.push_back(sensorValue);

	msg.header.stamp = ros::Time::now();
	msg.uri = names;
	msg.value = values;

	_pubAnalog.publish(msg);

	return 0;
}

auto PhidgetIKROS::setDigitalOutCallback(cob_phidgets::SetDigitalSensor::Request &req,
										cob_phidgets::SetDigitalSensor::Response &res) -> bool
{
	bool ret = false;
	_mutex.lock();
	_outputChanged.updated=false;
	_outputChanged.index=-1;
	_outputChanged.state=0;
	_mutex.unlock();

	this->setOutputState(req.index, req.state);

	ros::Time start = ros::Time::now();
	while((ros::Time::now().toSec() - start.toSec()) < 1.0)
	{
		_mutex.lock();
		if(_outputChanged.updated == true)
		{
			_mutex.unlock();
			break;
		}
		_mutex.unlock();

		ros::Duration(0.025).sleep();
	}
	_mutex.lock();
	res.index = _outputChanged.index;
	res.state = _outputChanged.state;
	ROS_DEBUG("Sending response: updated: %u, index: %d, state: %d",_outputChanged.updated, _outputChanged.index, _outputChanged.state);
	ret = (_outputChanged.updated && (_outputChanged.index == req.index));

	_mutex.unlock();

	return ret;
}
auto PhidgetIKROS::setDataRateCallback(cob_phidgets::SetDataRate::Request &req,
										cob_phidgets::SetDataRate::Response &res) -> bool
{
	this->setDataRate(req.index, req.data_rate);
	return true;
}
auto PhidgetIKROS::setTriggerValueCallback(cob_phidgets::SetTriggerValue::Request &req,
										cob_phidgets::SetTriggerValue::Response &res) -> bool
{
	this->setSensorChangeTrigger(req.index, req.trigger_value);
	return true;
}

auto PhidgetIKROS::attachHandler() -> int
{
	int serialNo, version, numInputs, numOutputs, millis;
	int numSensors, triggerVal, ratiometric, i;
	const char *ptr, *name;

	CPhidget_getDeviceName((CPhidgetHandle)_iKitHandle, &name);
	CPhidget_getDeviceType((CPhidgetHandle)_iKitHandle, &ptr);
	CPhidget_getSerialNumber((CPhidgetHandle)_iKitHandle, &serialNo);
	CPhidget_getDeviceVersion((CPhidgetHandle)_iKitHandle, &version);

	CPhidgetInterfaceKit_getInputCount(_iKitHandle, &numInputs);
	CPhidgetInterfaceKit_getOutputCount(_iKitHandle, &numOutputs);
	CPhidgetInterfaceKit_getSensorCount(_iKitHandle, &numSensors);
	CPhidgetInterfaceKit_getRatiometric(_iKitHandle, &ratiometric);

	ROS_INFO("%s %d attached!!", name, serialNo);

	ROS_DEBUG("%s", ptr);
	ROS_DEBUG("Serial Number: %d\tVersion: %d", serialNo, version);
	ROS_DEBUG("Num Digital Inputs: %d\tNum Digital Outputs: %d", numInputs, numOutputs);
	ROS_DEBUG("Num Sensors: %d", numSensors);
	ROS_DEBUG("Ratiometric: %d", ratiometric);

	for(i = 0; i < numSensors; i++)
	{
		CPhidgetInterfaceKit_getSensorChangeTrigger(_iKitHandle, i, &triggerVal);
		CPhidgetInterfaceKit_getDataRate(_iKitHandle, i, &millis);

		ROS_DEBUG("Sensor#: %d > Sensitivity Trigger: %d", i, triggerVal);
		ROS_DEBUG("Sensor#: %d > Data Rate: %d", i, millis);
	}

	return 0;
}

auto PhidgetIKROS::detachHandler() -> int
{
	int serial_number;
    const char *device_name;

    CPhidget_getDeviceName ((CPhidgetHandle)_iKitHandle, &device_name);
    CPhidget_getSerialNumber((CPhidgetHandle)_iKitHandle, &serial_number);
    ROS_INFO("%s Serial number %d detached!", device_name, serial_number);
    return 0;
}
