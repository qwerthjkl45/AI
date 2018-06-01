/*
 * main.cpp
 *
 *  Created on: 28 May 2016
 *      Author: Minh Ngo @ 3DUniversum
 */
 
#include <cmath>
#include <iostream>
#include <boost/format.hpp>

#include <pcl/point_types.h>
#include <pcl/point_cloud.h>
#include <pcl/features/integral_image_normal.h>
#include <pcl/visualization/pcl_visualizer.h>
#include <pcl/common/transforms.h>
#include <pcl/kdtree/kdtree_flann.h>
#include <pcl/surface/marching_cubes.h>
#include <pcl/surface/marching_cubes_hoppe.h>
#include <pcl/filters/passthrough.h>
#include <pcl/surface/poisson.h>
#include <pcl/surface/impl/texture_mapping.hpp>
#include <pcl/features/normal_3d_omp.h>

#include <eigen3/Eigen/Core>

#include <opencv2/opencv.hpp>
#include <opencv2/core/mat.hpp>
#include <opencv2/core/eigen.hpp>

#include "Frame3D/Frame3D.h"

pcl::PointCloud<pcl::PointXYZ>::Ptr mat2IntegralPointCloud(const cv::Mat& depth_mat, const float focal_length, const float max_depth) {
    // This function converts a depth image to a point cloud
    assert(depth_mat.type() == CV_16U);
    pcl::PointCloud<pcl::PointXYZ>::Ptr point_cloud(new pcl::PointCloud<pcl::PointXYZ>());
    const int half_width = depth_mat.cols / 2;
    const int half_height = depth_mat.rows / 2;
    const float inv_focal_length = 1.0 / focal_length;
    point_cloud->points.reserve(depth_mat.rows * depth_mat.cols);
    for (int y = 0; y < depth_mat.rows; y++) {
        for (int x = 0; x < depth_mat.cols; x++) {
            float z = depth_mat.at<ushort>(cv:: Point(x, y)) * 0.001;
            if (z < max_depth && z > 0) {
                point_cloud->points.emplace_back(static_cast<float>(x - half_width)  * z * inv_focal_length,
                                                 static_cast<float>(y - half_height) * z * inv_focal_length,
                                                 z);
            } else {
                point_cloud->points.emplace_back(x, y, NAN);
            }
        }
    }
    point_cloud->width = depth_mat.cols;
    point_cloud->height = depth_mat.rows;
    return point_cloud;
}


pcl::PointCloud<pcl::PointNormal>::Ptr computeNormals(pcl::PointCloud<pcl::PointXYZ>::Ptr cloud) {
    // This function computes normals given a point cloud
    // !! Please note that you should remove NaN values from the pointcloud after computing the surface normals.
    pcl::PointCloud<pcl::PointNormal>::Ptr cloud_normals(new pcl::PointCloud<pcl::PointNormal>); // Output datasets
    pcl::IntegralImageNormalEstimation<pcl::PointXYZ, pcl::PointNormal> ne;
    ne.setNormalEstimationMethod(ne.AVERAGE_3D_GRADIENT);
    ne.setMaxDepthChangeFactor(0.02f);
    ne.setNormalSmoothingSize(10.0f);
    ne.setInputCloud(cloud);
    ne.compute(*cloud_normals);
    pcl::copyPointCloud(*cloud, *cloud_normals);
    return cloud_normals;
}

pcl::PointCloud<pcl::PointXYZRGB>::Ptr transformPointCloud(pcl::PointCloud<pcl::PointXYZRGB>::Ptr cloud, const Eigen::Matrix4f& transform) {
    pcl::PointCloud<pcl::PointXYZRGB>::Ptr transformed_cloud(new pcl::PointCloud<pcl::PointXYZRGB>());
    pcl::transformPointCloud(*cloud, *transformed_cloud, transform);
    return transformed_cloud;
}


template<class T>
typename pcl::PointCloud<T>::Ptr transformPointCloudNormals(typename pcl::PointCloud<T>::Ptr cloud, const Eigen::Matrix4f& transform) {
    typename pcl::PointCloud<T>::Ptr transformed_cloud(new typename pcl::PointCloud<T>());
    pcl::transformPointCloudWithNormals(*cloud, *transformed_cloud, transform);
    return transformed_cloud;
}

pcl::PointCloud<pcl::PointXYZRGBNormal>::Ptr mergingPointClouds(Frame3D frames[]) {
    pcl::PointCloud<pcl::PointXYZRGBNormal>::Ptr modelCloud(new pcl::PointCloud<pcl::PointXYZRGBNormal>);

    for (int i = 0; i < 8; i++) {
        std::cout << boost::format("Merging frame %d") % i << std::endl;

        Frame3D frame = frames[i];
        cv::Mat depthImage = frame.depth_image_;
        double focalLength = frame.focal_length_;
        const Eigen::Matrix4f cameraPose = frame.getEigenTransform();

        // TODO(Student): Merge the i-th frame using predicted camera pose
        // to the global point cloud. ~ 20 lines.
        pcl::PointCloud<pcl::PointXYZ>::Ptr point_cloud = mat2IntegralPointCloud(depthImage, focalLength, 1.5);
        pcl::PointCloud<pcl::PointNormal>::Ptr point_cloud_with_normals = computeNormals(point_cloud);
        
        //remove NaN values from the pointcloud after computing the surface normals
        pcl::PointCloud<pcl::PointNormal>::Ptr point_cloud_with_normals_after_removal(new pcl::PointCloud<pcl::PointNormal>());
        point_cloud_with_normals_after_removal->points.reserve(point_cloud_with_normals->size());
        
        for (int idx = 0; idx < point_cloud_with_normals->size(); idx++) {
        
            if (std::isnan(point_cloud_with_normals->points[idx].normal_z) ||\  //Do you mean normal_x, _y, _z, instead of 3x _z ?
             std::isnan(point_cloud_with_normals->points[idx].normal_z) || \	//What is that \ for, behind the OR
             std::isnan(point_cloud_with_normals->points[idx].normal_z)){
                continue;
            }
            
            point_cloud_with_normals_after_removal->push_back(point_cloud_with_normals->points[idx]);
        }
        
        //transform point clouds
        point_cloud_with_normals = transformPointCloudNormals<pcl::PointNormal>(point_cloud_with_normals_after_removal, cameraPose);
        pcl::PointCloud<pcl::PointXYZRGBNormal>::Ptr modelCloud_tmp(new pcl::PointCloud<pcl::PointXYZRGBNormal>);
        pcl::copyPointCloud(*point_cloud_with_normals, *modelCloud_tmp);
        *modelCloud += *modelCloud_tmp;
    }  
    

    return modelCloud;
}

//@todo: have a look at function mapMultipleTexturesToMeshUV(..) !!
// in: http://docs.pointclouds.org/1.7.1/texture__mapping_8hpp_source.html
pcl::PointCloud<pcl::PointXYZRGBNormal>::Ptr mergingPointCloudsWithTexture(Frame3D frames[]) {
    pcl::PointCloud<pcl::PointXYZRGBNormal>::Ptr modelCloud(new pcl::PointCloud<pcl::PointXYZRGBNormal>);

	/* Start untested attempt: part 1 */
	//Okay, so it is mega confusing what they want you to do..., they want a PolygonMesh as input in
	//the pseudo-code, but it isn't inputted in this function.., so I create it using the function
	//mergingPointClouds(..) followed by the code in the bottom (before displaying)
	
	texturedCloud = mergingPointClouds(frames);

	pcl::PointCloud<pcl::PointXYZRGBNormal>::Ptr reduced_point_cloud(new pcl::PointCloud<pcl::PointXYZRGBNormal>());
	pcl::PassThrough<pcl::PointXYZRGBNormal> filter;

	filter.setInputCloud(texturedCloud);
	filter.filter(*reduced_point_cloud);
	
	// Create a mesh from the textured cloud using a reconstruction method,
	// Poisson Surface is currently hard-coded
	mesh = createMesh(reduced_point_cloud, 0);
	
	std::vector<pcl::Vertices> polygons = mesh.polygons;
	pcl::sensor_msgs::PointCloud2 point_cloud = mesh.cloud;
	/* End untested attempt: part 1 */
	
    for (int i = 0; i < 8; i++) {
        std::cout << boost::format("Merging frame %d") % i << std::endl;

        Frame3D frame = frames[i];
        cv::Mat depthImage = frame.depth_image_;
        double focalLength = frame.focal_length_;
        const Eigen::Matrix4f cameraPose = frame.getEigenTransform();

        // TODO(Student): The same as mergingPointClouds but now with texturing. ~ 50 lines.
        
		/* Start untested attempt: part 2 */
		transformed_point_cloud = transformPointCloud(point_cloud, camera_pose.inverse());
		
		for(int j = 0; j < polgons.size(); j++) {
			pcl::Vertices polygon = polygons[j];
			
			/*Not sure how to do this if-statement... 
			if(polygon.isVisible(toCameraPose))
				uv_coordinates = getUVCoordinates(polygon, transformed_point_cloud);
				//Assign the UV coordinates of this camera to the polygon
			*/
		}
		/* End untested attempt: part 2 */
    }

	//I expected it to the likelier to return polygons or something
    return modelCloud;
}

// Different methods of constructing mesh
enum CreateMeshMethod { PoissonSurfaceReconstruction = 0, MarchingCubes = 1};

// Create mesh from point cloud using one of above methods
pcl::PolygonMesh createMesh(pcl::PointCloud<pcl::PointXYZRGBNormal>::Ptr pointCloud, CreateMeshMethod method) {
    std::cout << "Creating meshes" << std::endl;

    // The variable for the constructed mesh
    pcl::PolygonMesh triangles;    
    switch (method) {
        case PoissonSurfaceReconstruction:
            // TODO(Student): Call Poisson Surface Reconstruction. ~ 5 lines.
            pcl::Poisson<pcl::PointXYZRGBNormal> poisson;
            poisson.setDepth(10);
            poisson.setInputCloud(pointCloud);
            poisson.reconstruct(triangles);   
            break;
        case MarchingCubes:
            // TODO(Student): Call Marching Cubes Surface Reconstruction. ~ 5 lines.
            double leafSize = 0.01; 
            int isoLevel = 3; 
            //Create search tree* 
            pcl::KdTree<pcl::PointXYZRGBNormal>::Ptr tree(new pcl::KdTreeFLANN<pcl::PointXYZRGBNormal>); 
			//Why KdTreeFLANN instead of a normal KdTree?
			
			//check: https://github.com/atduskgreg/pcl-marching-squares-example/blob/master/marching_cubes.cpp
			
            tree->setInputCloud(pointCloud); 
            //pcl::MarchingCubesGreedy<>
			
/* 			// based on the link above, this code should get it to work:	
			pcl::MarchingCubesRBF<PointNormal> mc;
			mc.setInputCloud (cloud_with_normals);
			mc.setSearchMethod (tree);
			mc.reconstruct (triangles);
 */            
			break;
    }
    return triangles;
}


int main(int argc, char *argv[]) {
    if (argc != 4) {
        std::cout << "./final [3DFRAMES PATH] [RECONSTRUCTION MODE] [TEXTURE_MODE]" << std::endl;

        return 0;
    }

    const CreateMeshMethod reconMode = static_cast<CreateMeshMethod>(std::stoi(argv[2]));

    // Loading 3D frames
    Frame3D frames[8];
    for (int i = 0; i < 8; ++i) {
        frames[i].load(boost::str(boost::format("%s/%05d.3df") % argv[1] % i));
    }

    pcl::PointCloud<pcl::PointXYZRGBNormal>::Ptr texturedCloud;
    pcl::PolygonMesh triangles;

    if (argv[3][0] == 't') {
        // SECTION 4: Coloring 3D Model
        // Create one point cloud by merging all frames with texture using
        // the rgb images from the frames
        texturedCloud = mergingPointCloudsWithTexture(frames);

        // Create a mesh from the textured cloud using a reconstruction method,
        // Poisson Surface or Marching Cubes
        triangles = createMesh(texturedCloud, reconMode);
    } else {
        // SECTION 3: 3D Meshing & Watertighting

        // Create one point cloud by merging all frames with texture using
        // the rgb images from the frames
        texturedCloud = mergingPointClouds(frames);

        pcl::PointCloud<pcl::PointXYZRGBNormal>::Ptr reduced_point_cloud(new pcl::PointCloud<pcl::PointXYZRGBNormal>());
        pcl::PassThrough<pcl::PointXYZRGBNormal> filter;

        filter.setInputCloud(texturedCloud);
        filter.filter(*reduced_point_cloud);
        
        // Create a mesh from the textured cloud using a reconstruction method,
        // Poisson Surface or Marching Cubes
        triangles = createMesh(reduced_point_cloud, reconMode);
        std::cout<<"create triangles"<<std::endl;
    }

    // Sample code for visualization.

    // Show viewer
    std::cout << "Finished texturing" << std::endl;
    boost::shared_ptr<pcl::visualization::PCLVisualizer> viewer(new pcl::visualization::PCLVisualizer("3D Viewer"));

    // Add colored point cloud to viewer, because it does not support colored meshes
    pcl::visualization::PointCloudColorHandlerRGBField<pcl::PointXYZRGBNormal> rgb(texturedCloud);
    viewer->addPointCloud<pcl::PointXYZRGBNormal>(texturedCloud, rgb, "cloud");

    // Add mesh
    viewer->setBackgroundColor(1, 1, 1);
    viewer->addPolygonMesh(triangles, "meshes", 0);
    viewer->addCoordinateSystem(1.0);
    viewer->initCameraParameters();

    // Keep viewer open
    while (!viewer->wasStopped()) {
        viewer->spinOnce(100);
        boost::this_thread::sleep(boost::posix_time::microseconds(100000));
    }


    return 0;
}
