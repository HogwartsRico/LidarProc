#include "LASSkeleton.h"
#include <set>
#include <iostream>
#include "../LidarBase/LASReader.h"
#include"../LidarGeometry/GeometryAlgorithm.h"
#include"../LidarGeometry/GeometryFlann.h"

using namespace GeometryLas;
using namespace std;
namespace LasAlgorithm
{
    int PointCloudShrinkSkeleton::PointCloudShrinkSkeleton_Centroid(Point3Ds pointSet, size_t* clusterIdx,int clusterNum)
    {
        //判断重心的标准为点到点簇中所有点的距离之和最小
        double disMin=999999999;
        int idxMin=0;

        //从性能上分析这个部分是可以进行优化的
        //TODO：不知道是不是有相应的算法，需要寻找
        for(int i=0;i<clusterNum;++i)
        {
            double disSigma=0;
            for(int j=0;j<clusterNum;++j)
            {
                disSigma+=DistanceComputation::Distance(pointSet[clusterIdx[i]],pointSet[clusterIdx[j]]);
            }
            idxMin=disSigma<disMin?i:idxMin;
            disMin=disSigma<disMin?disSigma:disMin;
            
        }
        return clusterIdx[idxMin];
    }

    Point3Ds PointCloudShrinkSkeleton::PointCloudShrinkSkeleton_Once(Point3Ds pointSet,int nearPointNum)
    {
        //构建kdtree
        typedef PointCloudAdaptor<std::vector<Point3D>> PCAdaptor;
		const PCAdaptor pcAdaptorPnts(pointSet);
		typedef KDTreeSingleIndexAdaptor<L2_Simple_Adaptor<double, PCAdaptor>, PCAdaptor, 3> kd_tree;
		kd_tree treeIndex(3, pcAdaptorPnts, KDTreeSingleIndexAdaptorParams(10));
		treeIndex.buildIndex();
        Point3Ds pntSkeSet;
        size_t *ret_index=new size_t[nearPointNum];
        double *out_dist_sqrt = new double[nearPointNum];
        set<int> idxSet;

        //看起来整个算法是串行算法，没法进行优化
        for(int i=0;i<pointSet.size();++i)
        {
            //首先找到K近邻的点
            double pnt[3]={pointSet[i].x,pointSet[i].y,pointSet[i].z};
            KNNResultSet<double> resultSet(nearPointNum);
            resultSet.init(ret_index, out_dist_sqrt);
            treeIndex.findNeighbors(resultSet,&pnt[0],SearchParams(10));

            //判断K近邻点中是否包含已有的中心点
            bool ifNotFind = false;
            for(int j=0;j<nearPointNum;++j)
            {
                ifNotFind = idxSet.find(ret_index[j])==idxSet.end()?true:false;
                if(!ifNotFind)
                    break;
            } 
            //如果不包含已有的中心点则在K近邻的点集中确定一个中心点
            if(ifNotFind)
            {
                int idxCentId = PointCloudShrinkSkeleton_Centroid(pointSet,ret_index,nearPointNum);
                pntSkeSet.push_back(pointSet[idxCentId]);
                idxSet.insert(idxCentId);
            }
        }
        delete[]ret_index;ret_index=nullptr;
        delete[]out_dist_sqrt;out_dist_sqrt=nullptr;
        idxSet.clear();
        
        return pntSkeSet;        
    }
    
    Point3Ds  PointCloudShrinkSkeleton::PointCloudShrinkSkeleton_Shrink(Point3Ds pointSet,int nearPointNum,int iteratorNum)
    {
        Point3Ds pnts(pointSet);
        Point3Ds pntTmp;
        for(int i=0;i<iteratorNum;++i)
        {
            pntTmp=PointCloudShrinkSkeleton_Once(pnts,nearPointNum);
// #ifdef _DEBUG
//             printf("iterator: %d points count: %d\n",i+1,pntTmp.size());
// #endif
            pnts.clear();
            pnts=pntTmp;
            pntTmp.clear();
        }
    
        return pnts;
    }    

    Point3Ds  PointCloudShrinkSkeleton::PointCloudShrinkSkeleton_Shrink(ILASDataset* lasDataset,int nearPointNum,int iteratorNum)
    {
        Point3Ds pointSet;
		for (int i = 0; i < lasDataset->m_totalReadLasNumber; ++i)
		{
			const LASIndex &idx = lasDataset->m_LASPointID[i];
			pointSet.push_back(lasDataset->m_lasRectangles[idx.rectangle_idx].m_lasPoints[idx.point_idx_inRect].m_vec3d);
		}
		return PointCloudShrinkSkeleton_Shrink(pointSet,nearPointNum,iteratorNum);   
    }
    ///////////////////////////////////////////////////////////////////////////////////////////////////////////
    /**
     * 直接根据直线拟合的方法能够获取线性特征比较强的块
     * 但是算法存在最大的问题在于在某些块中有线性特征比较强
     * 但是某些点偏差比较大，对于这些偏差大的点无法剔除
     **/
    bool PointCloudLineSkeleton::PointCloudLineSkeleton_LineExtractRaw(Point3Ds pointCluster,double threshold,MatrixXd &param)
    {
        //直线拟合
        //减去均值去中心化后平移的值为0，因此在处理过程中需要恢复，如何恢复平移的值还没有想好
        //先求缩放比例，然后代入回均值去求平移的值，如果只是判断是否为直线，则可以省略改过程
        double cx=0,cy=0,cz=0;
        for(int i=0;i<pointCluster.size();++i)
        {
            cx+=pointCluster[i].x/pointCluster.size();
            cy+=pointCluster[i].y/pointCluster.size();
            cz+=pointCluster[i].z/pointCluster.size();
        }
        for(int i=0;i<pointCluster.size();++i)
        {
            pointCluster[i].x-=cx;
            pointCluster[i].y-=cy;
            pointCluster[i].z-=cz;
        }

        //计算
        MatrixXd params = MatrixXd::Zero(2,2);
        MatrixXd paramM1= MatrixXd::Zero(2,2);
        MatrixXd paramM2= MatrixXd::Zero(2,2);
        for(int i=0;i<pointCluster.size();++i)
        {
            // printf("%lf-%lf-%lf\n",pointCluster[i].x,pointCluster[i].y,pointCluster[i].z);
            paramM1(0,0)+=pointCluster[i].x*pointCluster[i].z;
            paramM1(0,1)+=pointCluster[i].x;
            paramM1(1,0)+=pointCluster[i].y*pointCluster[i].z;
            paramM1(1,1)+=pointCluster[i].y;

            paramM2(0,0)+=pointCluster[i].z*pointCluster[i].z;
            paramM2(0,1)+=pointCluster[i].z;
            paramM2(1,0)+=pointCluster[i].z;
            paramM2(1,1)+=1;
        }
        params=paramM1*(paramM2.inverse());
        double residual=PointCloudLineSkeleton_LineResidual(pointCluster,params);
        param = params;
        //cout<<params<<endl;
        // direct.push_back(params(0,0));
        // direct.push_back(params(1,0));
        // direct.push_back(1.0);
        return residual<threshold;
    }

    //计算残差
    double PointCloudLineSkeleton::PointCloudLineSkeleton_LineResidual(Point3Ds pointCluster,MatrixXd lineParam)
    {
        double ex=0,ey=0,e=0;
        for(int i=0;i<pointCluster.size();++i)
        {
            MatrixXd mat(2,1);
            mat(0,0)=pointCluster[i].z;
            mat(1,0)=1;
            MatrixXd res=lineParam*mat;

            ex = res(0,0)-pointCluster[i].x;
            ey = res(1,0)-pointCluster[i].y;
            e+=sqrt(ex*ex+ey*ey);
        }
        return e/double(pointCluster.size());
    }


    Point3Ds PointCloudLineSkeleton::PointCloudLineSkeleton_Extract(Point3Ds pointSet,int nearPointNum,double lineResidual)
    {
        Point3Ds pnts(pointSet);
        Point3Ds pntTmp;
        for(int i=0;i<1;++i)
        {
            pntTmp=PointCloudLineSkeleton_Once(pointSet,nearPointNum,lineResidual);
#ifdef _DEBUG
            printf("iterator: %d points count: %d\n",i+1,pntTmp.size());
#endif
            pnts.clear();
            pnts=pntTmp;
            pntTmp.clear();
        }
        return pnts;
    }

    Point3Ds PointCloudLineSkeleton::PointCloudLineSkeleton_Extract(ILASDataset* lasDataset,int nearPointNum,double lineResidual)
    {
        Point3Ds pointSet;
		for (int i = 0; i < lasDataset->m_totalReadLasNumber; ++i)
		{
			const LASIndex &idx = lasDataset->m_LASPointID[i];
			pointSet.push_back(lasDataset->m_lasRectangles[idx.rectangle_idx].m_lasPoints[idx.point_idx_inRect].m_vec3d);
		}
		return PointCloudLineSkeleton_Extract(pointSet,nearPointNum,lineResidual); 
    }

    Point3Ds PointCloudLineSkeleton::PointCloudLineSkeleton_Once(Point3Ds pointSet,int nearPointNum,double lineResidual)
    {
        //构建kdtree
        typedef PointCloudAdaptor<std::vector<Point3D>> PCAdaptor;
		const PCAdaptor pcAdaptorPnts(pointSet);
		typedef KDTreeSingleIndexAdaptor<L2_Simple_Adaptor<double, PCAdaptor>, PCAdaptor, 3> kd_tree;
		kd_tree treeIndex(3, pcAdaptorPnts, KDTreeSingleIndexAdaptorParams(10));
		treeIndex.buildIndex();
        Point3Ds pntSkeSet;
        size_t *ret_index=new size_t[nearPointNum];
        double *out_dist_sqrt = new double[nearPointNum];
        vector<int> useLabel(pointSet.size());
        for(int i=0;i<pointSet.size();++i)
        {
            useLabel[i]=0;
        }

        //看起来整个算法是串行算法，没法进行优化
        MatrixXd direct;
        for(int i=0;i<pointSet.size();++i)
        {
            //首先找到K近邻的点
            Point3Ds temPtSet;
            double pnt[3]={pointSet[i].x,pointSet[i].y,pointSet[i].z};
            KNNResultSet<double> resultSet(nearPointNum);
            resultSet.init(ret_index, out_dist_sqrt);
            treeIndex.findNeighbors(resultSet,&pnt[0],SearchParams(10));
            for(int j=0;j<nearPointNum;++j)
            {
                temPtSet.push_back(pointSet[ret_index[j]]);
            }
            //判断是否具有线性特征
            if(PointCloudLineSkeleton_LineExtractRaw(temPtSet,lineResidual,direct))
            {
                for(int j=0;j<nearPointNum;++j)
                {
                    if(useLabel[ret_index[j]]==0)
                    {
                        useLabel[ret_index[j]]=1;
                        pntSkeSet.push_back(pointSet[ret_index[j]]);
                    }
                }
            }
        }
        delete[]ret_index;ret_index=nullptr;
        delete[]out_dist_sqrt;out_dist_sqrt=nullptr;
        useLabel.clear();
        return pntSkeSet;                
    }

    void PointCloudLineSkeleton::PointCloudShrinkSkeleton_LineTest()
    {
        Point3Ds points(50);
        for (int i = 0; i < 50; i++)
        {
            points[i].z = (double)i; 
            points[i].x = 3*points[i].z + 1 + double(rand())/double(RAND_MAX);  //引入噪声
            points[i].y = 2*points[i].z + 2 + double(rand())/double(RAND_MAX);  //引入噪声
        }
        MatrixXd direct;
        PointCloudLineSkeleton_LineExtractRaw(points,5,direct);
    }

    ////////////////////////////////////////////////////////////////////////////////////////////////////////

    //精化线拟合
    vector<int> PointCloudLineRefineSkeleton::PointCloudLineSkeleton_LineRefine(Point3D ptCnt,Point3Ds pointCluster,double threshold)
    {
        vector<int> lineRefineIdxs;
        MatrixXd direct;
        GeometryRelation geoRel;
        double cx=0,cy=0,cz=0;
        for(int i=0;i<pointCluster.size();++i)
        {
            cx+=pointCluster[i].x/pointCluster.size();
            cy+=pointCluster[i].y/pointCluster.size();
            cz+=pointCluster[i].z/pointCluster.size();
        }
        for(int i=0;i<pointCluster.size();++i)
        {
            pointCluster[i].x-=cx;
            pointCluster[i].y-=cy;
            pointCluster[i].z-=cz;
        }
        if(PointCloudLineSkeleton_LineExtractRaw(pointCluster,threshold,direct))
        {
            //整体的判断有问题，在这里给出的是残差而不是距离阈值，需要进一步思考
            //先去中心化，然后直接判断角度
            //在具有线性特征的情况下进一步获取具体的点
            Point3D blockDirect(0,direct(1,1)-direct(1,0)*(direct(0,1)/direct(0,0)),-direct(0,1)/direct(0,0));
            for(int i=0;i<pointCluster.size();++i)
            {
                double distance=DistanceComputation::Distance(ptCnt,pointCluster[i]);
                //方向的计算有问题
                //虚拟于主方向上任何一个点理论上都可以，只需要了解到大小的趋势就行
                Point3D ptDirect(pointCluster[i].x-blockDirect.x,pointCluster[i].y-blockDirect.y,pointCluster[i].z-blockDirect.z);
                double vecterAngle = geoRel.VectorAngle(blockDirect,ptDirect);
                //判断(综合考虑方向因素后的距离是否小于原始距离的60%)
                if(vecterAngle<0.2)
                {
                    lineRefineIdxs.push_back(i);
                }
            }
        }
        return lineRefineIdxs;
    }

    Point3Ds PointCloudLineRefineSkeleton::PointCloudLineSkeleton_Once(Point3Ds pointSet,int nearPointNum,double lineResidual)
    {
        //构建kdtree
        typedef PointCloudAdaptor<std::vector<Point3D>> PCAdaptor;
		const PCAdaptor pcAdaptorPnts(pointSet);
		typedef KDTreeSingleIndexAdaptor<L2_Simple_Adaptor<double, PCAdaptor>, PCAdaptor, 3> kd_tree;
		kd_tree treeIndex(3, pcAdaptorPnts, KDTreeSingleIndexAdaptorParams(10));
		treeIndex.buildIndex();
        Point3Ds pntSkeSet;
        size_t *ret_index=new size_t[nearPointNum];
        double *out_dist_sqrt = new double[nearPointNum];
        vector<int> useLabel(pointSet.size());
        for(int i=0;i<pointSet.size();++i)
        {
            useLabel[i]=0;
        }

        //看起来整个算法是串行算法，没法进行优化
        MatrixXd direct;
        for(int i=0;i<pointSet.size();++i)
        {
            //首先找到K近邻的点
            Point3Ds temPtSet;
            double pnt[3]={pointSet[i].x,pointSet[i].y,pointSet[i].z};
            KNNResultSet<double> resultSet(nearPointNum);
            resultSet.init(ret_index, out_dist_sqrt);
            treeIndex.findNeighbors(resultSet,&pnt[0],SearchParams(10));
            for(int j=0;j<nearPointNum;++j)
            {
                temPtSet.push_back(pointSet[ret_index[j]]);
            }
            //判断是否具有线性特征
            if(PointCloudLineSkeleton_LineExtractRaw(temPtSet,lineResidual,direct))
            {
                //获取强线性特征的点集
                vector<int> lineRef;
                lineRef=PointCloudLineSkeleton_LineRefine(pointSet[i],temPtSet,lineResidual);
                // printf("%d\n",lineRef.size());
                for(int j=0;j<lineRef.size();++j)
                {
                    int idx = lineRef[j];
                    if(useLabel[ret_index[idx]]==0)
                    {
                        useLabel[ret_index[idx]]=1;
                        pntSkeSet.push_back(pointSet[ret_index[idx]]);
                    }
                }
            }
        }
        delete[]ret_index;ret_index=nullptr;
        delete[]out_dist_sqrt;out_dist_sqrt=nullptr;
        useLabel.clear();
        return pntSkeSet;      
    }


    Point3Ds PointCloudLineRefineSkeleton::PointCloudLineSkeleton_Extract(Point3Ds pointSet,int nearPointNum,double lineResidual)
    {
        Point3Ds pnts(pointSet);
        Point3Ds pntTmp;
        for(int i=0;i<1;++i)
        {
            pntTmp=PointCloudLineSkeleton_Once(pointSet,nearPointNum,lineResidual);
// #ifdef _DEBUG
//             printf("iterator: %d points count: %d\n",i+1,pntTmp.size());
// #endif
            pnts.clear();
            pnts=pntTmp;
            pntTmp.clear();
        }
        return pnts;
    }

    Point3Ds PointCloudLineRefineSkeleton::PointCloudLineSkeleton_Extract(ILASDataset* lasDataset,int nearPointNum,double lineResidual)
    {
        Point3Ds pointSet;
		for (int i = 0; i < lasDataset->m_totalReadLasNumber; ++i)
		{
			const LASIndex &idx = lasDataset->m_LASPointID[i];
			pointSet.push_back(lasDataset->m_lasRectangles[idx.rectangle_idx].m_lasPoints[idx.point_idx_inRect].m_vec3d);
		}
		return PointCloudLineSkeleton_Extract(pointSet,nearPointNum,lineResidual); 
    }

}