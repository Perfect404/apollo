/******************************************************************************
 * Copyright 2017 The Apollo Authors. All Rights Reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *****************************************************************************/

#include "modules/planning/lattice/behavior_decider/path_time_neighborhood.h"

#include <utility>
#include <vector>
#include <cmath>

#include "modules/planning/proto/sl_boundary.pb.h"
#include "modules/planning/common/obstacle.h"
#include "modules/planning/lattice/util/lattice_params.h"
#include "modules/planning/lattice/util/reference_line_matcher.h"
#include "modules/planning/lattice/util/lattice_util.h"

namespace apollo {
namespace planning {

using apollo::common::PathPoint;
using apollo::common::TrajectoryPoint;
using apollo::perception::PerceptionObstacle;

PathTimeNeighborhood::PathTimeNeighborhood(
    const Frame* frame,
    const std::array<double, 3>& init_s,
    const ReferenceLine& reference_line,
    const std::vector<common::PathPoint>& discretized_ref_points) {

  init_s_ = init_s;
  SetupObstacles(frame, reference_line, discretized_ref_points);
}

void PathTimeNeighborhood::SetupObstacles(
    const Frame* frame,
    const ReferenceLine& reference_line,
    const std::vector<common::PathPoint>& discretized_ref_points) {
  const auto& obstacles = frame->obstacles();

  for (const Obstacle* obstacle : obstacles) {
    if (obstacle->Trajectory().trajectory_point_size() == 0) {
      continue;
    }

    double relative_time = 0.0;
    while (relative_time < planned_trajectory_time) {
      common::TrajectoryPoint point = obstacle->GetPointAtTime(relative_time);
      common::math::Box2d box = obstacle->GetBoundingBox(point);

      // TODO(all) Remove raw reference line and reimplement GetSlBoundary
      SLBoundary sl_boundary;
      reference_line.GetSLBoundary(box, &sl_boundary);

      /**
       // TODO(all) confirm the logic to determine whether an obstacle
       // is forward or backward.
       if (sl_boundary.end_s() < init_s_[0]) {
       backward_obstacle_id_set_.insert(obstacle->Id());
       } else {
       forward_obstacle_id_set_.insert(obstacle->Id());
       }
       **/

      //the obstacle is not shown on the region to be considered.
      if (sl_boundary.end_s() < 0.0
          || sl_boundary.start_s() > init_s_[0] + planned_trajectory_horizon
          || (std::abs(sl_boundary.start_l()) > lateral_enter_lane_thred
              && std::abs(sl_boundary.end_l()) > lateral_enter_lane_thred)) {
        if (path_time_obstacle_map_.find(obstacle->Id())
            != path_time_obstacle_map_.end()) {
          break;
        } else {
          relative_time += trajectory_time_resolution;
          continue;
        }
      }

      double v = SpeedOnReferenceLine(discretized_ref_points, obstacle,
          sl_boundary);

      if (path_time_obstacle_map_.find(obstacle->Id())
          == path_time_obstacle_map_.end()) {
        path_time_obstacle_map_[obstacle->Id()].set_obstacle_id(obstacle->Id());

        *path_time_obstacle_map_[obstacle->Id()].mutable_bottom_left() =
            SetPathTimePoint(obstacle->Id(), sl_boundary.start_s(), relative_time);

        *path_time_obstacle_map_[obstacle->Id()].mutable_upper_left() =
            SetPathTimePoint(obstacle->Id(), sl_boundary.end_s(), relative_time);
      }

      *path_time_obstacle_map_[obstacle->Id()].mutable_bottom_right() =
          SetPathTimePoint(obstacle->Id(), sl_boundary.start_s(), relative_time);

      *path_time_obstacle_map_[obstacle->Id()].mutable_upper_right() =
          SetPathTimePoint(obstacle->Id(), sl_boundary.end_s(), relative_time);
    }
    relative_time += trajectory_time_resolution;
  }

  for (auto& path_time_obstacle : path_time_obstacle_map_) {
    double s_upper = std::max(path_time_obstacle.second.bottom_right().s(),
        path_time_obstacle.second.upper_right().s());

    double s_lower = std::min(path_time_obstacle.second.bottom_left().s(),
        path_time_obstacle.second.upper_left().s());

    path_time_obstacle.second.set_path_lower(s_lower);

    path_time_obstacle.second.set_path_upper(s_upper);

    double t_upper = std::max(path_time_obstacle.second.bottom_right().t(),
        path_time_obstacle.second.upper_right().t());

    double t_lower = std::min(path_time_obstacle.second.bottom_left().t(),
        path_time_obstacle.second.upper_left().t());

    path_time_obstacle.second.set_time_lower(t_lower);

    path_time_obstacle.second.set_time_upper(t_upper);
  }
}

PathTimePoint PathTimeNeighborhood::SetPathTimePoint(const std::string& obstacle_id,
    const double s, const double t) const {
  PathTimePoint path_time_point;
  path_time_point.set_s(s);
  path_time_point.set_t(t);
  path_time_point.set_obstacle_id(obstacle_id);
  return path_time_point;
}

double PathTimeNeighborhood::SpeedOnReferenceLine(
    const std::vector<PathPoint>& discretized_ref_points,
    const Obstacle* obstacle, const SLBoundary& sl_boundary) {
  PathPoint obstacle_point_on_ref_line =
      ReferenceLineMatcher::MatchToReferenceLine(discretized_ref_points,
          sl_boundary.start_s());
  const PerceptionObstacle& perception_obstacle = obstacle->Perception();
  double ref_theta = obstacle_point_on_ref_line.theta();
  auto velocity = perception_obstacle.velocity();
  double v = std::cos(ref_theta) * velocity.x()
      + std::sin(ref_theta) * velocity.y();
  return v;
}

std::vector<PathTimeObstacle> PathTimeNeighborhood::GetPathTimeObstacles() const {
  std::vector<PathTimeObstacle> path_time_obstacles;
  for (const auto& path_time_obstacle_element : path_time_obstacle_map_) {
    path_time_obstacles.push_back(path_time_obstacle_element.second);
  }
  return path_time_obstacles;
}

bool PathTimeNeighborhood::GetPathTimeObstacle(const std::string& obstacle_id,
    PathTimeObstacle* path_time_obstacle) {
  /**
   if (forward_obstacle_id_set_.find(obstacle_id) ==
   forward_obstacle_id_set_.end() ||
   backward_obstacle_id_set_.find(obstacle_id) ==
   backward_obstacle_id_set_.end()) {
   return false;
   }
   **/
  if (path_time_obstacle_map_.find(obstacle_id) == path_time_obstacle_map_.end()) {
    return false;
  }
  *path_time_obstacle = path_time_obstacle_map_[obstacle_id];
  return true;
}

/**
 bool ADCNeighborhood::ForwardNearestObstacle(
 std::array<double, 3>* forward_nearest_obstacle_state, double* enter_time) {
 bool found = false;
 for (const auto& obstacle_state : forward_neighborhood_) {
 double obstacle_s = obstacle_state[1];
 // TODO(all) consider the length of adc,
 // Maybe change init_s_[0] to init_s_[0] - half_adc_length
 if (!found) {
 found = true;
 *enter_time = obstacle_state[0];
 (*forward_nearest_obstacle_state)[0] = obstacle_state[1];
 (*forward_nearest_obstacle_state)[1] = obstacle_state[3];
 (*forward_nearest_obstacle_state)[2] = obstacle_state[4];
 } else if (obstacle_s < (*forward_nearest_obstacle_state)[0]) {
 *enter_time = obstacle_state[0];
 (*forward_nearest_obstacle_state)[0] = obstacle_state[1];
 (*forward_nearest_obstacle_state)[1] = obstacle_state[3];
 (*forward_nearest_obstacle_state)[2] = obstacle_state[4];
 }
 }
 return found;
 }

 bool ADCNeighborhood::BackwardNearestObstacle(
 std::array<double, 3>* backward_nearest_obstacle_state,
 double* enter_time) {
 bool found = false;
 for (const auto& obstacle_state : backward_neighborhood_) {
 double obstacle_s = obstacle_state[2];
 // TODO(all) consider the length of adc,
 // Maybe change init_s_[0] to init_s_[0] + half_adc_length
 if (obstacle_s > init_s_[0]) {
 continue;
 }
 if (!found) {
 found = true;
 *enter_time = obstacle_state[0];
 (*backward_nearest_obstacle_state)[0] = obstacle_state[2];
 (*backward_nearest_obstacle_state)[1] = obstacle_state[3];
 (*backward_nearest_obstacle_state)[2] = obstacle_state[4];
 } else if (obstacle_s > (*backward_nearest_obstacle_state)[0]) {
 *enter_time = obstacle_state[0];
 (*backward_nearest_obstacle_state)[0] = obstacle_state[2];
 (*backward_nearest_obstacle_state)[1] = obstacle_state[3];
 (*backward_nearest_obstacle_state)[2] = obstacle_state[4];
 }
 }
 return found;
 }

 bool ADCNeighborhood::IsForward(const Obstacle* obstacle) const {
 std::string obstacle_id = obstacle->Id();
 return forward_obstacle_id_set_.find(obstacle_id) !=
 forward_obstacle_id_set_.end();
 }

 bool ADCNeighborhood::IsBackward(const Obstacle* obstacle) const {
 std::string obstacle_id = obstacle->Id();
 return backward_obstacle_id_set_.find(obstacle_id) !=
 backward_obstacle_id_set_.end();
 }

 bool ADCNeighborhood::IsInNeighborhood(const Obstacle* obstacle) const {
 return IsForward(obstacle) || IsBackward(obstacle);
 }
 **/

}  // namespace planning
}  // namespace apollo