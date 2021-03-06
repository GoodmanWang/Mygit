#include "mvt.h"
using namespace boost::math;

#define MVT_CDF_MAPPING_SIZE 10000
#define MVT_CDF_MAPPING_MEAN (MVT_CDF_MAPPING_SIZE/2)

static void CDF_mapping(double std, int* mapping, int* p_x_min, int* p_x_max , int* mapping_inv)
{
	normal_distribution<double> normal_x(0, std);

	int x_min = -std*3;
	int x_max =  std*3;
	double val_min = boost::math::cdf(normal_x,(double)x_min-1);
	double val_max = boost::math::cdf(normal_x,(double)x_max);
	double val_dist = val_max - val_min;
	unsigned int idx_start = 0;
	unsigned int idx_end = 0;
	for( int x=x_min; x<x_max; x++ )
	{
		double val = boost::math::cdf(normal_x,(double)x);
		idx_end = (unsigned int)(MVT_CDF_MAPPING_SIZE * (val-val_min) / val_dist);

		mapping[x-x_min] = idx_end;
		for( unsigned int i=idx_start; i<idx_end; i++ )
		{
			mapping_inv[i] = x;
		}

		idx_start = idx_end;
	}

	for( unsigned int i=idx_start; i<MVT_CDF_MAPPING_SIZE; i++ )
	{
		mapping_inv[i] = x_max;
	}

	(*p_x_min) = x_min;
	(*p_x_max) = x_max-1;
}

static cv::Point2d* centers               = NULL;
static cv::Point2d* centers_rectified_ref = NULL;
static double* centers_root_x             = NULL;
static double* centers_root_y             = NULL;
static cv::Point2d* centers_new           = NULL;
static cv::Point*   centers_rectified_new = NULL;

MVT_Sampling::MVT_Sampling(MVT_Param param, MVT_3D_Object* pObject3d)
{
	m_num_of_samples = param.num_of_partcenter_sample;
	centers               = (cv::Point2d*)malloc(sizeof(cv::Point2d)*m_num_of_samples);
	centers_rectified_ref = (cv::Point2d*)malloc(sizeof(cv::Point2d)*m_num_of_samples);
	centers_root_x = (double*)malloc(sizeof(double)*m_num_of_samples);
	centers_root_y = (double*)malloc(sizeof(double)*m_num_of_samples);
	centers_new           = (cv::Point2d*)malloc(sizeof(cv::Point2d)*m_num_of_samples);
	centers_rectified_new = (cv::Point*  )malloc(sizeof(cv::Point  )*m_num_of_samples);

	// The number of parts & roots
	m_num_of_partsNroots = pObject3d->Num_of_PartsNRoots();
	m_num_of_parts       = pObject3d->Num_of_Parts();

	// Boundary for viewpoint (note: azimuth is circular)
	std::vector<MVT_ELEVATION> elevation_dics = pObject3d->GetDiscElevation();
	MVT_ELEVATION elevation_stepsize = elevation_dics[1] - elevation_dics[0];
	m_elevation_min = elevation_dics[0];
	m_elevation_max = elevation_dics[1] + elevation_stepsize/2;

	std::vector<MVT_DISTANCE> distance_dics = pObject3d->GetDiscDistance();
	MVT_DISTANCE distance_stepsize = distance_dics[1] - distance_dics[0];
	m_distance_min = distance_dics[0];// - distance_stepsize/2;
	m_distance_max = distance_dics[distance_dics.size()-1];

	// Boundary for center locations
	m_x_min = 0;
	m_x_max = 0;
	m_y_min = 0;
	m_y_max = 0;
	m_x_rectified_min = (unsigned int*)malloc(sizeof(unsigned int)*m_num_of_partsNroots);
	m_x_rectified_max = (unsigned int*)malloc(sizeof(unsigned int)*m_num_of_partsNroots);
	m_y_rectified_min = (unsigned int*)malloc(sizeof(unsigned int)*m_num_of_partsNroots);
	m_y_rectified_max = (unsigned int*)malloc(sizeof(unsigned int)*m_num_of_partsNroots);

	// Cumulative Distribution for each part
	m_cdf_x_rectified_min         = (int*)malloc(sizeof(int)*m_num_of_partsNroots);
	m_cdf_x_rectified_max         = (int*)malloc(sizeof(int)*m_num_of_partsNroots);
	m_cdf_y_rectified_min         = (int*)malloc(sizeof(int)*m_num_of_partsNroots);
	m_cdf_y_rectified_max         = (int*)malloc(sizeof(int)*m_num_of_partsNroots);
	m_cdf_inv_x_rectified_min     = (int*)malloc(sizeof(int)*m_num_of_partsNroots);
	m_cdf_inv_x_rectified_max     = (int*)malloc(sizeof(int)*m_num_of_partsNroots);
	m_cdf_inv_y_rectified_min     = (int*)malloc(sizeof(int)*m_num_of_partsNroots);
	m_cdf_inv_y_rectified_max     = (int*)malloc(sizeof(int)*m_num_of_partsNroots);
	m_cdf_mapping_x_rectified     = (int**)malloc(sizeof(int*)*m_num_of_partsNroots);
	m_cdf_mapping_y_rectified     = (int**)malloc(sizeof(int*)*m_num_of_partsNroots);
	m_cdf_mapping_inv_x_rectified = (int**)malloc(sizeof(int*)*m_num_of_partsNroots);
	m_cdf_mapping_inv_y_rectified = (int**)malloc(sizeof(int*)*m_num_of_partsNroots);

	// Standard derivation for continuous viewpoint and center locations
	m_std_sampling.std_azimuth   = param.std_azimuth;
	m_std_sampling.std_elevation = param.std_elevation;
	m_std_sampling.std_distance  = param.std_distance;
	m_std_sampling.std_x_partsNroots.resize(m_num_of_partsNroots);
	m_std_sampling.std_y_partsNroots.resize(m_num_of_partsNroots);
	for( unsigned int pr=0; pr<m_num_of_partsNroots; pr++ )
	{
		MVT_2D_Part_Front part_front = pObject3d->GetPartFrontInfo(pr);

		if( pr < m_num_of_parts )
		{
			m_std_sampling.std_x_partsNroots[pr] = part_front.width/4;
			m_std_sampling.std_y_partsNroots[pr] = part_front.height/4;
		}
		else
		{
			m_std_sampling.std_x_partsNroots[pr] = part_front.width/32;
			m_std_sampling.std_y_partsNroots[pr] = part_front.height/32;
		}

		m_x_rectified_min[pr] = 0;//(unsigned int)part_front.width/2;
		m_x_rectified_max[pr] = 0;
		m_y_rectified_min[pr] = 0;//(unsigned int)part_front.height/2;
		m_y_rectified_max[pr] = 0;

		m_cdf_mapping_x_rectified[pr]     = (int*)malloc(sizeof(int)*part_front.width*6);
		m_cdf_mapping_y_rectified[pr]     = (int*)malloc(sizeof(int)*part_front.height*6);
		m_cdf_mapping_inv_x_rectified[pr] = (int*)malloc(sizeof(int)*MVT_CDF_MAPPING_SIZE);
		m_cdf_mapping_inv_y_rectified[pr] = (int*)malloc(sizeof(int)*MVT_CDF_MAPPING_SIZE);

		CDF_mapping(m_std_sampling.std_x_partsNroots[pr],  m_cdf_mapping_x_rectified[pr], &(m_cdf_x_rectified_min[pr]), &(m_cdf_x_rectified_max[pr]), m_cdf_mapping_inv_x_rectified[pr]);
		CDF_mapping(m_std_sampling.std_y_partsNroots[pr], m_cdf_mapping_y_rectified[pr], &(m_cdf_y_rectified_min[pr]), &(m_cdf_y_rectified_max[pr]), m_cdf_mapping_inv_y_rectified[pr]);
	}

	// Center locations for a reference state
	m_centers_part_x_ref = (double*)malloc(sizeof(double)*m_num_of_partsNroots);
	m_centers_part_y_ref = (double*)malloc(sizeof(double)*m_num_of_partsNroots);

	// Random Generator
	m_rng = new cv::RNG(1);
	cv::theRNG().state = 1;
}

MVT_Sampling::~MVT_Sampling()
{
	free(centers);
	free(centers_rectified_ref);
	free(centers_root_x);
	free(centers_root_y);
	free(centers_new);
	free(centers_rectified_new);

	free( m_centers_part_x_ref );
	free( m_centers_part_y_ref );

	free( m_x_rectified_min );
	free( m_x_rectified_max );
	free( m_y_rectified_min );
	free( m_y_rectified_max );

	for( unsigned int pr=0; pr<m_num_of_partsNroots; pr++ )
	{
		free( m_cdf_mapping_x_rectified[pr] );
		free( m_cdf_mapping_y_rectified[pr] );
		free( m_cdf_mapping_inv_x_rectified[pr] );
		free( m_cdf_mapping_inv_y_rectified[pr] );
	}
	free( m_cdf_mapping_x_rectified );
	free( m_cdf_mapping_y_rectified );
	free( m_cdf_mapping_inv_x_rectified );
	free( m_cdf_mapping_inv_y_rectified );

	free( m_cdf_x_rectified_min );
	free( m_cdf_x_rectified_max );
	free( m_cdf_y_rectified_min );
	free( m_cdf_y_rectified_max );
	free( m_cdf_inv_x_rectified_min );
	free( m_cdf_inv_x_rectified_max );
	free( m_cdf_inv_y_rectified_min );
	free( m_cdf_inv_y_rectified_max );
}

void MVT_Sampling::SetRefState(MVT_State* p_state_ref)
{
	MVT_2D_Object* pObject2d = p_state_ref->pObject2d;

	m_azimuth_ref   = p_state_ref->viewpoint.azimuth;
	m_elevation_ref = p_state_ref->viewpoint.elevation;
	m_distance_ref  = p_state_ref->viewpoint.distance;

	m_center_root_x_ref = p_state_ref->center_root.x;
	m_center_root_y_ref = p_state_ref->center_root.y;

	m_bbox_ref = p_state_ref->bbox_root;

	m_x_min = 0;
	m_x_max = pObject2d->GetImage()->cols-1;
	m_y_min = 0;
	m_y_max = pObject2d->GetImage()->rows-1;

	for( unsigned int pr=0; pr<m_num_of_partsNroots; pr++)
	{
		if( !pObject2d->IsOccluded(pr) )
		{
			if( pr < m_num_of_parts )
			{
				m_centers_part_x_ref[pr] = p_state_ref->centers[pr].x;
				m_centers_part_y_ref[pr] = p_state_ref->centers[pr].y;
			}
			else
			{
				m_centers_part_x_ref[pr] = m_center_root_x_ref;
				m_centers_part_y_ref[pr] = m_center_root_y_ref;
			}
		}
		else
		{
			m_centers_part_x_ref[pr] = m_center_root_x_ref + pObject2d->m_2dparts[pr].center.x;
			m_centers_part_y_ref[pr] = m_center_root_y_ref + pObject2d->m_2dparts[pr].center.y;
		}
	}
}

bool MVT_Sampling::IsValidViewpoint(MVT_2D_Object* pObject2d)
{
	for( unsigned int pr=0; pr<m_num_of_partsNroots; pr++ )
	{
		if( !pObject2d->IsOccluded(pr) )
		{
			cv::Mat* pImage_rectified = pObject2d->GetRectifiedImage(pr);
			if(
				( m_x_rectified_min[pr]*2 >= (unsigned int)pImage_rectified->cols ) ||
				( m_y_rectified_min[pr]*2 >= (unsigned int)pImage_rectified->rows )
			)
			{
				return false;
			}
		}
	}

	return true;
}

MVT_Viewpoint MVT_Sampling::Sampling_Viewpoint()
{
	// continuous viewpoint sampling
	MVT_Viewpoint viewpoint;

	if( g_b_initializing )
	{
		viewpoint.azimuth   = m_rng->uniform(0,360);
		ValidateAzimuth(viewpoint.azimuth);

		do{
			viewpoint.elevation = m_rng->uniform(m_elevation_min,m_elevation_max);
		}while(!IsValidElevation(viewpoint.elevation));

		do{
			viewpoint.distance  = m_rng->uniform(m_distance_min,m_distance_max);
		}while(!IsValidDistance(viewpoint.distance));
	}
	else
	{
		viewpoint.azimuth   = m_rng->gaussian(m_std_sampling.std_azimuth)   + m_azimuth_ref;
		ValidateAzimuth(viewpoint.azimuth);

		do{
			viewpoint.elevation = m_rng->gaussian(m_std_sampling.std_elevation) + m_elevation_ref;
		}while(!IsValidElevation(viewpoint.elevation));

		do{
			viewpoint.distance  = m_rng->gaussian(m_std_sampling.std_distance)  + m_distance_ref;
			if( viewpoint.distance > m_distance_max )
			{
				viewpoint.distance = m_distance_max;
			}
		}while(!IsValidDistance(viewpoint.distance));
	}

	return viewpoint;
}

void MVT_Sampling::Sampling_Centers(MVT_State* states, unsigned int n_states, cv::Rect &bbox)
{
/*
#ifdef _OPENMP
#pragma omp parallel for
#endif
*/
	for( unsigned int s=0; s<n_states; s++ )
	{
		while(true)
		{
			int x,y,w,h;

			if(bbox.width > 0 && states->likelihood_root[MVT_LIKELIHOOD_DPM] > g_param.thresh2_dpm)
			{
				x = (int)(m_rng->gaussian( m_bbox_ref.width/8   ) + m_center_root_x_ref); // ORG *1   seq /8
				y = (int)(m_rng->gaussian( m_bbox_ref.height/8  ) + m_center_root_y_ref); // ORG *1   seq /8
				w = (int)(m_rng->gaussian( m_bbox_ref.width /16 ) + m_bbox_ref.width);
				h = (int)(m_rng->gaussian( m_bbox_ref.height/16 ) + m_bbox_ref.height);
			}
			else
			{
				int x_min = m_x_min + m_bbox_ref.width/2;
				int x_max = m_x_max - m_bbox_ref.width/2;
				int y_min = m_y_min + m_bbox_ref.height/2;
				int y_max = m_y_max - m_bbox_ref.height/2;

				x = (int)(m_rng->uniform((double)x_min,(double)x_max)); // ORG *1
				y = (int)(m_rng->uniform((double)y_min,(double)y_max));  // ORG *1
				w = (int)(m_rng->gaussian( m_bbox_ref.width /32 ) + m_bbox_ref.width);
				h = (int)(m_rng->gaussian( m_bbox_ref.height/32 ) + m_bbox_ref.height);
			}

			if( !( x < (int)m_x_min || (int)m_x_max < x ||
				   y < (int)m_y_min || (int)m_y_max < y    )
			)
			{
				states[s].center_root.x = x;
				states[s].center_root.y = y;
				if(bbox.width>0)
				{
					states[s].bbox_root.x = x - bbox.width/2;
					states[s].bbox_root.y = y - bbox.height/2;
				}
				else
				{
					states[s].bbox_root.x = x - w/2;
					states[s].bbox_root.y = y - h/2;
				}
				states[s].bbox_root.width = w;
				states[s].bbox_root.height = h;
				break;
			}
		}
	}
}

void MVT_Sampling::Sampling_PartsCenters(MVT_2D_Object* pObject2d, MVT_State* p_state_ret, unsigned int n_states)
{
#ifdef _OPENMP
#pragma omp parallel for
#endif
	for(unsigned int s=0; s<n_states; s++)
	{
		centers_root_x[s] = m_center_root_x_ref;
		centers_root_y[s] = m_center_root_y_ref;
	}

	for( int pr=(int)(m_num_of_partsNroots-1); pr>=0; pr-- ) // Sample the root location first
	{
		if( !pObject2d->IsOccluded(pr) )
		{
			double x_relative = m_centers_part_x_ref[pr] - m_center_root_x_ref;
			double y_relative = m_centers_part_y_ref[pr] - m_center_root_y_ref;

			cv::Mat* pImage_rectified = pObject2d->GetRectifiedImage(pr);
			m_x_rectified_max[pr] = pImage_rectified->cols-1;//image_rectified.cols - m_x_rectified_min[pr];
			m_y_rectified_max[pr] = pImage_rectified->rows-1;//image_rectified.rows - m_y_rectified_min[pr];

#ifdef _OPENMP
#pragma omp parallel for
#endif
			for(unsigned int s=0; s<n_states; s++)
			{
				centers[s].x = centers_root_x[s] + x_relative;
				centers[s].y = centers_root_y[s] + y_relative;
			}
			pObject2d->GetRectifiedPoint(pr, centers, n_states, centers_rectified_ref);

			RandomsNormal(pr, centers_rectified_ref, centers_rectified_new, n_states);

			pObject2d->GetRestorePoints(pr, centers_rectified_new, n_states, centers_new);

			if( pr>=(int)m_num_of_parts )
			{
#ifdef _OPENMP
#pragma omp parallel for
#endif
				for(unsigned int s=0; s<n_states; s++)
				{
					centers_root_x[s] = centers_new[s].x;
					centers_root_y[s] = centers_new[s].y;

					p_state_ret[s].center_root = centers_new[s];
				}
			}
		}
		else
		{
				double x_ = pObject2d->GetPartInfo(pr).center.x;
				double y_ = pObject2d->GetPartInfo(pr).center.y;
#ifdef _OPENMP
#pragma omp parallel for
#endif
				for(unsigned int s=0; s<n_states; s++)
				{
					centers_new[s].x = x_ + centers_root_x[s];
					centers_new[s].y = y_ + centers_root_y[s];
					centers_rectified_new[s].x = 0;
					centers_rectified_new[s].y = 0;
				}
		}

#ifdef _OPENMP
#pragma omp parallel for
#endif
		for(unsigned int s=0; s<n_states; s++)
		{
			p_state_ret[s].centers[pr]           = centers_new[s];
			p_state_ret[s].centers_rectified[pr] = centers_rectified_new[s];
			p_state_ret[s].bbox_partsNroots      = pObject2d->GetTargetBoundingBox(p_state_ret[s]);
		}
	}
}

static cv::RNG rng(0);
static cv::Point RandomNormal(cv::Point2d mean,
                                int      x_min, int     x_max, int     y_min, int     y_max,
                                int  cdf_x_min, int cdf_x_max, int cdf_y_min, int cdf_y_max,
                                int* cdf_mapping_x,     int* cdf_mapping_y,
                                int* cdf_mapping_inv_x, int* cdf_mapping_inv_y
)
{
	int x_min_relative = (int)(x_min-mean.x);
	int x_max_relative = (int)(x_max-mean.x);
	int y_min_relative = (int)(y_min-mean.y);
	int y_max_relative = (int)(y_max-mean.y);

	// Generate [X]
	int x_ret, y_ret;
	if( x_min_relative >= cdf_x_max )
	{
		x_ret = x_min;
	}
	else if( x_max_relative <= cdf_x_min )
	{
		x_ret = x_max;
	}
	else
	{
		if( x_min_relative < cdf_x_min ) x_min_relative = cdf_x_min;
		if( x_max_relative > cdf_x_max ) x_max_relative = cdf_x_max;

		int idx_x_cdf = rng.uniform( cdf_mapping_x[x_min_relative-cdf_x_min],
										cdf_mapping_x[x_max_relative-cdf_x_min] );
		x_ret = (int)(cdf_mapping_inv_x[idx_x_cdf] + mean.x);
	}

	// Generate [Y]
	if( y_min_relative >= cdf_y_max )
	{
		y_ret = y_min;
	}
	else if( y_max_relative <= cdf_y_min )
	{
		y_ret = y_max;
	}
	else
	{
		if( y_min_relative < cdf_y_min ) y_min_relative = cdf_y_min;
		if( y_max_relative > cdf_y_max ) y_max_relative = cdf_y_max;

		int idx_y_cdf = rng.uniform( cdf_mapping_y[y_min_relative-cdf_y_min],
										cdf_mapping_y[y_max_relative-cdf_y_min] );
		y_ret = (int)(cdf_mapping_inv_y[idx_y_cdf] + mean.y);
	}

	// Make Sure
	if( x_ret < (int)x_min ) x_ret = (int)x_min;
	if(	x_ret > (int)x_max ) x_ret = (int)x_max;
	if(	y_ret < (int)y_min ) y_ret = (int)y_min;
	if(	y_ret > (int)y_max ) y_ret = (int)y_max;

	return cv::Point(x_ret,y_ret);
}

void MVT_Sampling::RandomsNormal( unsigned int idx_part, cv::Point2d* means, cv::Point* points, unsigned int n_points )
{

#ifdef _OPENMP
#pragma omp parallel for
#endif
	for( unsigned int p=0; p<n_points; p++ )
	{
		points[p] = ::RandomNormal(means[p],
							(int)(m_x_rectified_min[idx_part]), (int)(m_x_rectified_max[idx_part]), (int)(m_y_rectified_min[idx_part]),   (int)(m_y_rectified_max[idx_part]),
							m_cdf_x_rectified_min[idx_part], 	     m_cdf_x_rectified_max[idx_part],      m_cdf_y_rectified_min[idx_part],       m_cdf_y_rectified_max[idx_part],
							m_cdf_mapping_x_rectified[idx_part],     m_cdf_mapping_y_rectified[idx_part],
							m_cdf_mapping_inv_x_rectified[idx_part], m_cdf_mapping_inv_y_rectified[idx_part] );
	}
}

void MVT_Sampling::ValidateAzimuth(MVT_AZIMUTH& azimuth)
{
	while( azimuth < 0 || 360 <= azimuth )
	{
		if(azimuth<0)    azimuth+=360;
		if(azimuth>=360) azimuth-=360;
	}
}
