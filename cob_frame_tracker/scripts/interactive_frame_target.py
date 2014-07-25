#!/usr/bin/python
import sys
import math
import copy
from copy import deepcopy

import roslib 
roslib.load_manifest('cob_frame_tracker')

import rospy
import tf
from std_srvs.srv import Empty
from cob_srvs.srv import SetString
from geometry_msgs.msg import Point, PoseStamped
from visualization_msgs.msg import Marker, InteractiveMarker, InteractiveMarkerControl
from interactive_markers.interactive_marker_server import *
from interactive_markers.menu_handler import *


class InteractiveFrameTarget:
	def __init__(self, active_frame, tracking_frame):
		self.active_frame = active_frame
		self.tracking_frame = tracking_frame
		
		print "Waiting for StartTrackingServer..."
		rospy.wait_for_service('start_tracking')
		print "...done!"
		self.start_tracking_client = rospy.ServiceProxy('start_tracking', SetString)
		
		print "Waiting for StopTrackingServer..."
		rospy.wait_for_service('stop_tracking')
		print "...done!"
		self.stop_tracking_client = rospy.ServiceProxy('stop_tracking', Empty)
		
		self.target_pose = PoseStamped()
		self.target_pose.header.stamp = rospy.Time.now()
		self.target_pose.header.frame_id = "base_link"
		self.target_pose.pose.orientation.w = 1.0
		self.br = tf.TransformBroadcaster()
		self.listener = tf.TransformListener()
		
		transform_available = False
		while not transform_available:
			try:
				(trans,rot) = self.listener.lookupTransform('/base_link', self.active_frame, rospy.Time(0))
			except (tf.LookupException, tf.ConnectivityException, tf.ExtrapolationException):
				rospy.logwarn("Waiting for transform...")
				rospy.sleep(0.1)
				continue
			transform_available = True
		
		self.target_pose.pose.position.x = trans[0]
		self.target_pose.pose.position.y = trans[1]
		self.target_pose.pose.position.z = trans[2]
		self.target_pose.pose.orientation.x = rot[0]
		self.target_pose.pose.orientation.y = rot[1] 
		self.target_pose.pose.orientation.z = rot[2]
		self.target_pose.pose.orientation.w = rot[3]
		
		self.ia_server = InteractiveMarkerServer("marker_server")
		self.int_marker = InteractiveMarker()
		self.int_marker.header.frame_id = "base_link"
		self.int_marker.pose = self.target_pose.pose
		self.int_marker.name = "interactive_target"
		self.int_marker.description = self.tracking_frame
		self.int_marker.scale = 0.8
		
		# create a grey box marker
		box_marker = Marker()
		box_marker.type = Marker.CUBE
		box_marker.scale.x = 0.1
		box_marker.scale.y = 0.1
		box_marker.scale.z = 0.1
		box_marker.color.r = 0.0
		box_marker.color.g = 0.5
		box_marker.color.b = 0.5
		box_marker.color.a = 1.0
		box_control = InteractiveMarkerControl()
		box_control.always_visible = True
		box_control.markers.append( box_marker )
		self.int_marker.controls.append(box_control)
		
		control = InteractiveMarkerControl()
		control.always_visible = True
		control.orientation.w = 1
		control.orientation.x = 1
		control.orientation.y = 0
		control.orientation.z = 0
		control.name = "move_3D"
		control.interaction_mode = InteractiveMarkerControl.MOVE_3D
		self.int_marker.controls.append(deepcopy(control))
		control.name = "move_x"
		control.interaction_mode = InteractiveMarkerControl.MOVE_AXIS
		self.int_marker.controls.append(deepcopy(control))
		control.name = "rotate_x"
		control.interaction_mode = InteractiveMarkerControl.ROTATE_AXIS
		self.int_marker.controls.append(deepcopy(control))
		control.name = "move_y"
		control.orientation.x = 0
		control.orientation.y = 1
		control.interaction_mode = InteractiveMarkerControl.MOVE_AXIS
		self.int_marker.controls.append(deepcopy(control))
		control.name = "rotate_y"
		control.interaction_mode = InteractiveMarkerControl.ROTATE_AXIS
		self.int_marker.controls.append(deepcopy(control))
		control.name = "move_z"
		control.orientation.y = 0
		control.orientation.z = 1
		control.interaction_mode = InteractiveMarkerControl.MOVE_AXIS
		self.int_marker.controls.append(deepcopy(control))
		control.name = "rotate_z"
		control.interaction_mode = InteractiveMarkerControl.ROTATE_AXIS
		self.int_marker.controls.append(deepcopy(control))
		
		self.ia_server.insert(self.int_marker, self.marker_fb)
		
		#create menu
		self.menu_handler = MenuHandler()
		self.menu_handler.insert( "StartTracking", callback=self.start_tracking )
		self.menu_handler.insert( "StopTracking", callback=self.stop_tracking )
		self.menu_handler.insert( "ResetTracking", callback=self.reset_tracking )
		self.int_marker_menu = InteractiveMarker()
		self.int_marker_menu.header.frame_id = "base_link"
		self.int_marker_menu.name = "marker_menu"
		self.int_marker_menu.description = rospy.get_namespace()
		self.int_marker_menu.scale = 1.0
		self.int_marker_menu.pose.position.z = 1.2
		control = InteractiveMarkerControl()
		control.interaction_mode = InteractiveMarkerControl.MENU
		control.name = "menu_control"
		control.description= "InteractiveTargetMenu"
		self.int_marker_menu.controls.append(copy.deepcopy(control))
		self.ia_server.insert(self.int_marker_menu, self.menu_fb)
		self.menu_handler.apply( self.ia_server, self.int_marker_menu.name )
		self.ia_server.applyChanges()
	
	def start_tracking(self, fb):
		#print "start_tracking pressed"
		try:
			res = self.start_tracking_client(data=self.tracking_frame)
			print res
		except rospy.ServiceException, e:
			print "Service call failed: %s"%e
	
	def stop_tracking(self, fb):
		#print "stop_tracking pressed"
		try:
			res = self.stop_tracking_client()
			print res
		except rospy.ServiceException, e:
			print "Service call failed: %s"%e
	
	def reset_tracking(self, fb):
		#print "reset_tracking pressed"
		self.stop_tracking(fb)
		
		transform_available = False
		while not transform_available:
			try:
				(trans,rot) = self.listener.lookupTransform('/base_link', self.active_frame, rospy.Time(0))
			except (tf.LookupException, tf.ConnectivityException, tf.ExtrapolationException):
				rospy.logwarn("Waiting for transform...")
				rospy.sleep(0.1)
				continue
			transform_available = True
		
		reset_pose = PoseStamped()
		reset_pose.header.frame_id = "/base_link"
		reset_pose.header.stamp = rospy.Time.now()
		reset_pose.pose.position.x = trans[0]
		reset_pose.pose.position.y = trans[1]
		reset_pose.pose.position.z = trans[2]
		reset_pose.pose.orientation.x = rot[0]
		reset_pose.pose.orientation.y = rot[1] 
		reset_pose.pose.orientation.z = rot[2]
		reset_pose.pose.orientation.w = rot[3]
		
		self.target_pose = reset_pose
		self.ia_server.setPose(self.int_marker.name, reset_pose.pose);
		self.ia_server.applyChanges()
	
	def menu_fb(self, fb):
		pass
	
	def marker_fb(self, fb):
		#p = feedback.pose.position
		#print feedback.marker_name + " is now at " + str(p.x) + ", " + str(p.y) + ", " + str(p.z)
		self.target_pose.header = fb.header
		self.target_pose.pose = fb.pose
		self.ia_server.applyChanges()

 	def run(self):
		self.br.sendTransform((self.target_pose.pose.position.x, self.target_pose.pose.position.y, self.target_pose.pose.position.z), tf.transformations.quaternion_from_euler(0, 0, 0), rospy.Time.now(), self.tracking_frame, self.target_pose.header.frame_id)


if __name__ == "__main__":
	rospy.init_node("interactive_frame_target")
	
	#get this from the frame_tracker parameters
	if rospy.has_param('active_frame'):
		active_frame = rospy.get_param("active_frame")
	else:
		rospy.logerr("No active_frame specified. Aborting!")
		sys.exit()
	if rospy.has_param('tracking_frame'):
		tracking_frame = rospy.get_param("tracking_frame")
	else:
		rospy.logerr("No tracking_frame specified. Aborting!")
		sys.exit()
	
	ilt = InteractiveFrameTarget(active_frame, tracking_frame)

 	r = rospy.Rate(68)
 	while not rospy.is_shutdown():
   		ilt.run()
		r.sleep()
