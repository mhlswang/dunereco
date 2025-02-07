////////////////////////////////////////////////////////////////////////
// Class:       dunefd::ShSeg
// Module Type: producer
// File:        ShSeg_module.cc
//
// Generated by Dorota Stefan using artmod
// from cetpkgsupport v1_08_07.
//
// This module is useful for testing purposes and should be used only with simulated showers. 
// It computes dE/dx [MeV/cm] along the shower axis.
//
// BuildSegMC(e):  
//				it builds an axis using MC direction and MC primary vertex;
//				unclear vertex region, as we expect in neutrino vertex, is taken into account.
//  
// useful variables for various tests of hit reconstruction:
// fDedxavg [MeV/cm]: averaged dE/dx, (one per shower).
// fDedx    [MeV/cm]: dE/dx computed for each hit in the shower (several per shower).
////////////////////////////////////////////////////////////////////////

#include "art/Framework/Core/EDProducer.h"
#include "art/Framework/Core/ModuleMacros.h"
#include "art/Framework/Principal/Event.h"
#include "art/Framework/Principal/Handle.h"
#include "art/Framework/Principal/Run.h"
#include "art/Framework/Principal/SubRun.h"
#include "art_root_io/TFileService.h"
#include "canvas/Utilities/InputTag.h"
#include "fhiclcpp/ParameterSet.h" 
#include "messagefacility/MessageLogger/MessageLogger.h"

#include "larcore/Geometry/Geometry.h"
#include "lardataobj/RecoBase/Track.h"
#include "lardataobj/RecoBase/Hit.h"
#include "lardataobj/RecoBase/Cluster.h"
#include "lardataobj/RecoBase/Vertex.h"
#include "lardataobj/RecoBase/SpacePoint.h"
#include "lardataobj/RecoBase/PFParticle.h"

#include "lardata/DetectorInfoServices/DetectorClocksService.h"
#include "lardata/DetectorInfoServices/DetectorPropertiesService.h"
#include "lardata/Utilities/AssociationUtil.h"
#include "larreco/Calorimetry/CalorimetryAlg.h"

#include "larsim/MCCheater/ParticleInventoryService.h"
#include "nusimdata/SimulationBase/MCTruth.h"

#include "larreco/RecoAlg/ProjectionMatchingAlg.h"
#include "larreco/RecoAlg/PMAlg/Utilities.h"
#include "larreco/RecoAlg/PMAlg/PmaTrack3D.h"

#include <random>

#include "TTree.h"

namespace dunefd 
{
	class ShSeg;
}

class dunefd::ShSeg : public art::EDProducer {
public:
  explicit ShSeg(fhicl::ParameterSet const & p);
  // The destructor generated by the compiler is fine for classes
  // without bare pointers or other resource use.

  // Plugins should not be copied or assigned.
  ShSeg(ShSeg const &) = delete;
  ShSeg(ShSeg &&) = delete;
  ShSeg & operator = (ShSeg const &) = delete;
  ShSeg & operator = (ShSeg &&) = delete;

  void beginJob() override;

  void reconfigure(fhicl::ParameterSet const& p) ;

  void produce(art::Event & e) override;

private:
	bool InsideFidVol(TLorentzVector const & pvtx) const;
	
	void FilterOutSmallParts(
                detinfo::DetectorPropertiesData const& detProp,
		double r2d, 
		const std::vector< art::Ptr<recob::Hit> >& hits_in,
					std::vector< art::Ptr<recob::Hit> >& hits_out);

	bool GetCloseHits(
                detinfo::DetectorPropertiesData const& detProp,
		double r2d, 
		const std::vector< art::Ptr<recob::Hit> >& hits_in, 
		std::vector<size_t>& used,
		std::vector< art::Ptr<recob::Hit> >& hits_out);

	bool Has(const std::vector<size_t>& v, size_t idx);

        void CorrOffset(detinfo::DetectorPropertiesData const& detProp,
                        TVector3& vec, const simb::MCParticle& particle);

	recob::Track ConvertFrom(pma::Track3D const & src);

	void Smearel(); void Smearph();	

	bool BuildSegMC(art::Event & e);

	std::vector< pma::Track3D* > fPmatracks;
  
	double fFidVolCut;
	double fR0; double fR1; 
	short isdata;

	std::string fHitsModuleLabel;
	calo::CalorimetryAlg fCalorimetryAlg;
	pma::ProjectionMatchingAlg fProjectionMatchingAlg;

	int fTrkindex;
	int fEvNumber;
	double fDedxavg;
	double fDedx;

	TTree* fEvTree; 
	TTree* fShTree; 
};


/******************************************************************************/

dunefd::ShSeg::ShSeg(fhicl::ParameterSet const & p) : EDProducer{p},
	fCalorimetryAlg(p.get<fhicl::ParameterSet>("CalorimetryAlg")),
	fProjectionMatchingAlg(p.get< fhicl::ParameterSet >("ProjectionMatchingAlg"))	
	{
		fR0 = 0.0; fR1 = 0.0;
		fTrkindex = 0; 
		fEvNumber = -1; fDedxavg = -1.0; fDedx = -1.0;
	
		fPmatracks.clear();
		this->reconfigure(p);

		produces< std::vector<recob::Track> >();
		produces< std::vector<recob::SpacePoint> >();
		produces< art::Assns<recob::Track, recob::Hit> >();
		produces< art::Assns<recob::Track, recob::SpacePoint> >();
		produces< art::Assns<recob::SpacePoint, recob::Hit> >();
	}

/******************************************************************************/

void dunefd::ShSeg::beginJob()
{
	art::ServiceHandle<art::TFileService> tfs;

	fEvTree = tfs->make<TTree>("Egtestev", "egtestev");
	fEvTree->Branch("fEvNumber", &fEvNumber, "fEvNumber/I");
	fEvTree->Branch("fDedxavg", &fDedxavg, "fDedxavg/D");

	fShTree = tfs->make<TTree>("Egtestsh", "egtestsh");
	fShTree->Branch("fDedx", &fDedx, "fDedx/D");
}

/******************************************************************************/

void dunefd::ShSeg::reconfigure(fhicl::ParameterSet const & p)
{
	fHitsModuleLabel	= p.get< std::string >("HitsModuleLabel");
	fFidVolCut		= p.get< double >("FidVolCut");
  	return;
}

/******************************************************************************/

void dunefd::ShSeg::produce(art::Event & e)
{
	isdata = e.isRealData();
	fEvNumber = e.id().event();

	std::unique_ptr< std::vector< recob::Track > > tracks(new std::vector< recob::Track >);
	std::unique_ptr< std::vector< recob::SpacePoint > > allsp(new std::vector< recob::SpacePoint >);

	std::unique_ptr< art::Assns< recob::Track, recob::Hit > > trk2hit(new art::Assns< recob::Track, recob::Hit >);
	std::unique_ptr< art::Assns< recob::Track, recob::SpacePoint > > trk2sp(new art::Assns< recob::Track, recob::SpacePoint >);
	std::unique_ptr< art::Assns< recob::SpacePoint, recob::Hit > > sp2hit(new art::Assns< recob::SpacePoint, recob::Hit >);

	if (!isdata) BuildSegMC(e);

	// if (!isdata) BuildSegReco(e);

	if (fPmatracks.size())
	{
		size_t spStart = 0, spEnd = 0;
		double sp_pos[3], sp_err[6];
		for (size_t i = 0; i < 6; i++) sp_err[i] = 1.0;

		fTrkindex = 0;
		for (auto trk : fPmatracks)
		{
			tracks->push_back(ConvertFrom(*trk));
			fTrkindex++;

			std::vector< art::Ptr< recob::Hit > > hits2d;
			art::PtrVector< recob::Hit > sp_hits;
			spStart = allsp->size();

			for (int h = trk->size() - 1; h >= 0; h--)
			{
				pma::Hit3D* h3d = (*trk)[h];
				if (!h3d->IsEnabled()) continue;
				hits2d.push_back(h3d->Hit2DPtr());

				if ((h == 0) ||
					(sp_pos[0] != h3d->Point3D().X()) ||
					(sp_pos[1] != h3d->Point3D().Y()) ||
					(sp_pos[2] != h3d->Point3D().Z()))
					{
						if (sp_hits.size()) // hits assigned to the previous sp
						{
							util::CreateAssn(*this, e, *allsp, sp_hits, *sp2hit);
							sp_hits.clear();
						}
						sp_pos[0] = h3d->Point3D().X();
						sp_pos[1] = h3d->Point3D().Y();
						sp_pos[2] = h3d->Point3D().Z();
						allsp->push_back(recob::SpacePoint(sp_pos, sp_err, 1.0));
					}
					sp_hits.push_back(h3d->Hit2DPtr());
				}
				if (sp_hits.size()) // hits assigned to the last sp
				{
					util::CreateAssn(*this, e, *allsp, sp_hits, *sp2hit);
				}
				spEnd = allsp->size();

				if (hits2d.size())
				{
					util::CreateAssn(*this, e, *tracks, *allsp, *trk2sp, spStart, spEnd);
					util::CreateAssn(*this, e, *tracks, hits2d, *trk2hit);
				}
			}

		// data prods done, delete all pma::Track3D's
		for (size_t t = 0; t < fPmatracks.size(); t++) delete fPmatracks[t];
		fPmatracks.clear();
	}

	e.put(std::move(tracks));
	e.put(std::move(allsp));
	e.put(std::move(trk2hit));
	e.put(std::move(trk2sp));
	e.put(std::move(sp2hit));
}

/******************************************************************************/

recob::Track dunefd::ShSeg::ConvertFrom(pma::Track3D const & src)
{
	std::vector< TVector3 > xyz, dircos;

	for (size_t i = 0; i < src.size(); ++i)
		if (src[i]->IsEnabled())
	{
		xyz.push_back(src[i]->Point3D());

		if (i < src.size() - 1)
		{
			TVector3 dc(src[i + 1]->Point3D());
			dc -= src[i]->Point3D();
			dc *= 1.0 / dc.Mag();
			dircos.push_back(dc);
		}
		else dircos.push_back(dircos.back());
	}

	if (xyz.size() != dircos.size())
	{
		mf::LogError("IniSegReco") << "pma::Track3D to recob::Track conversion problem.";
	}
	return recob::Track(recob::TrackTrajectory(recob::tracking::convertCollToPoint(xyz),
						   recob::tracking::convertCollToVector(dircos),
						   recob::Track::Flags_t(xyz.size()), false),
			    0, -1., 0, recob::tracking::SMatrixSym55(), recob::tracking::SMatrixSym55(), fTrkindex);
}

/******************************************************************************/

bool dunefd::ShSeg::BuildSegMC(art::Event & e)
{
	bool result = true;

	std::vector<art::Ptr<recob::Hit> > hitlist;
	auto hitListHandle = e.getHandle< std::vector<recob::Hit> >(fHitsModuleLabel);
	if (hitListHandle)
		art::fill_ptr_vector(hitlist, hitListHandle);

	art::ServiceHandle<cheat::ParticleInventoryService> pi_serv;
	art::ServiceHandle<geo::Geometry> geom;
	const sim::ParticleList& plist = pi_serv->ParticleList();

	std::vector<const simb::MCParticle * > primaries = plist.GetPrimaries();
	if (primaries.size() != 1)  return false;
	
	const simb::MCParticle* firstel = primaries[0];
	if ((firstel->PdgCode() != 11) && (firstel->PdgCode() != -11) && (firstel->PdgCode() != 22)) return false;	

	TLorentzVector startingp = primaries[0]->Position();
	TVector3 primaryvtx(startingp.X(), startingp.Y(), startingp.Z());
	
	// pretend that there is vertex which can hide situation in it.
	// cut hits randomly wth gauss dist (both: el, gammas)
	Smearel();
	if ((firstel->PdgCode() == 22) && (firstel->EndProcess() == "conv")) 
	{
		startingp = primaries[0]->EndPosition();
		// smear with normal dist (only for gammas)
		Smearph();
	}

	// check fiducial volume
	if (!InsideFidVol(startingp)) return false;

	TLorentzVector mom = primaries[0]->Momentum();
	TVector3 momvec3(mom.Px(), mom.Py(), mom.Pz());
	TVector3 dir = momvec3 * (1 / momvec3.Mag());

	TVector3 firstpoint(startingp.X(), startingp.Y(), startingp.Z());	
	TVector3 secondpoint = firstpoint + dir;

	// offset corrections
        auto const clockData = art::ServiceHandle<detinfo::DetectorClocksService const>()->DataFor(e);
        auto const detProp = art::ServiceHandle<detinfo::DetectorPropertiesService const>()->DataFor(e, clockData);
        CorrOffset(detProp, primaryvtx, *firstel);
        CorrOffset(detProp, firstpoint, *firstel);
        CorrOffset(detProp, secondpoint, *firstel);

	// check if it is inside
        geo::TPCID tpcid = geom->FindTPCAtPosition(geo::vect::toPoint(firstpoint));
	if (!tpcid.isValid) return false;

	// try to build seg, it is based on MC truth 
	size_t tpc = tpcid.TPC;
        size_t cryo = geom->PositionToCryostatID(geo::vect::toPoint(firstpoint)).Cryostat;
	pma::Track3D* iniseg = new pma::Track3D();
        iniseg->AddNode(detProp, firstpoint, tpc, cryo);
        iniseg->AddNode(detProp, secondpoint, tpc, cryo);

	for (size_t h = 0; h < hitlist.size(); h++)
		if (hitlist[h]->WireID().TPC == tpc)
                        iniseg->push_back(detProp, hitlist[h]);
		
	iniseg->MakeProjection();
	iniseg->SortHits();
		
	// only fraction of hits are interesting
	size_t hi = 0;
	while (hi < iniseg->size())
	{
		pma::Hit3D* hit3d = (*iniseg)[hi];
				
		if ((hit3d->GetSegFraction() > 20) || (hit3d->GetSegFraction() < 0)) 
		{
			iniseg->release_at(hi);
			continue;
		}	
		hi++;
	}			

	// check hits size in the track
	if (iniseg->size() < 5) return false;

	// 0: geo::kU, 1: geo::kV, 2: geo::kZ
	const geo::TPCGeo& tpcgeo = geom->GetElement(tpcid);
	double maxdist = 0.0; size_t bestview = 0; 
	
	for (size_t view = 0; view < tpcgeo.Nplanes(); ++view) 
	{
		std::map< size_t, std::vector< double > > ex;
		iniseg->GetRawdEdxSequence(ex, view);
	
		TVector2 proj_i = pma::GetProjectionToPlane(firstpoint, view, tpc, cryo); 
		TVector2 proj_f = pma::GetProjectionToPlane(secondpoint, view, tpc, cryo); 
		double dist = std::sqrt(pma::Dist2(proj_i, proj_f));
		if ((dist > maxdist) && (ex.size() > 0))
		{
			maxdist = dist;
			bestview = view;
		}
	}

	
	fPmatracks.push_back(iniseg);
	/************************************/
	
        iniseg->CompleteMissingWires(detProp, bestview);
	std::map< size_t, std::vector< double > > dedx;
	iniseg->GetRawdEdxSequence(dedx, bestview);
	double sumdx = 0.0; fDedxavg = 0.0;

	double rmin = 1.0e9;
	for (auto & v: dedx)
		if (v.second[7] < rmin) rmin = v.second[7];

	double rmax = rmin + 3.0;

	for (auto & v: dedx)
		if (((v.second[7] + fR1) > fR0) && (v.second[7] < 15))
		{
                        double dE = fCalorimetryAlg.dEdx_AREA(clockData, detProp, v.second[5], v.second[1], bestview);
			double range = v.second[7] + (fR0 - fR1);

			v.second[5] = dE;				
			v.second[7] = range;

			if ((v.second[5] > 0) && (v.second[6] > 0))
			{
				fDedx = v.second[5] / v.second[6];
				fShTree->Fill();

				if (v.second[7] < rmax)
				{
					fDedxavg += v.second[5];
					sumdx += v.second[6];
				}
			}
		}

	if (sumdx > 0)	fDedxavg = fDedxavg / sumdx;
	fEvTree->Fill();

	return result;
}

/******************************************************************************/

void dunefd::ShSeg::CorrOffset(detinfo::DetectorPropertiesData const& detProp,
                               TVector3& vec, const simb::MCParticle& particle)
{
	float corrt0x = particle.T() * 1.e-3 * detProp.DriftVelocity();

	float px = vec.X();
	if (px > 0) px -= corrt0x;
	else px += corrt0x;
	vec.SetX(px);
}

/******************************************************************************/

void dunefd::ShSeg::Smearel()
{
	std::random_device rd;
    	std::mt19937 gen(rd());
	
	double mean = 0.0; double sigma = 0.7;
	std::normal_distribution<double> dist (mean, sigma);	

	fR0 = fabs(dist(gen));
}

/******************************************************************************/

void dunefd::ShSeg::Smearph() 
{
	std::random_device rd;
    	std::mt19937 gen(rd());

	std::uniform_real_distribution<double> distribution(0.0, fR0);
	fR1 = distribution(gen);
}

/******************************************************************************/

bool dunefd::ShSeg::InsideFidVol(TLorentzVector const & pvtx) const
{
	art::ServiceHandle<geo::Geometry> geom;
        geo::Point_t const vtx{pvtx.X(), pvtx.Y(), pvtx.Z()};
	bool inside = false;

	geo::TPCID idtpc = geom->FindTPCAtPosition(vtx);

	if (geom->HasTPC(idtpc))
	{		
		const geo::TPCGeo& tpcgeo = geom->GetElement(idtpc);
		double minx = tpcgeo.MinX(); double maxx = tpcgeo.MaxX();
		double miny = tpcgeo.MinY(); double maxy = tpcgeo.MaxY();
		double minz = tpcgeo.MinZ(); double maxz = tpcgeo.MaxZ();

		//x
		double dista = fabs(minx - pvtx.X());
		double distb = fabs(pvtx.X() - maxx); 

		if ((pvtx.X() > minx) && (pvtx.X() < maxx) &&
		 	(dista > fFidVolCut) && (distb > fFidVolCut))
		{ 
			inside = true;
		}
		else { inside = false; }

		//y
		dista = fabs(maxy - pvtx.Y());
		distb = fabs(pvtx.Y() - miny);
		if (inside && (pvtx.Y() > miny) && (pvtx.Y() < maxy) &&
		 	(dista > fFidVolCut) && (distb > fFidVolCut)) inside = true;
		else inside = false;

		//z
		dista = fabs(maxz - pvtx.Z());
		distb = fabs(pvtx.Z() - minz);
		if (inside && (pvtx.Z() > minz) && (pvtx.Z() < maxz) &&
		 	(dista > fFidVolCut) && (distb > fFidVolCut)) inside = true;
		else inside = false;
	}
		
	return inside;
}

/***********************************************************************/

bool dunefd::ShSeg::Has(const std::vector<size_t>& v, size_t idx)
{
    for (auto c : v) if (c == idx) return true;
    return false;
}

/***********************************************************************/

void dunefd::ShSeg::FilterOutSmallParts(
                detinfo::DetectorPropertiesData const& detProp,
		double r2d,
		const std::vector< art::Ptr<recob::Hit> >& hits_in,
		std::vector< art::Ptr<recob::Hit> >& hits_out)
{
	size_t min_size = hits_in.size() / 5;
	if (min_size < 3) min_size = 3;

	std::vector<size_t> used;
	std::vector< art::Ptr<recob::Hit> > close_hits;
	
        while (GetCloseHits(detProp, r2d, hits_in, used, close_hits))
	{
		if (close_hits.size() > min_size)
			for (auto h : close_hits) hits_out.push_back(h);
	}
}

/***********************************************************************/

bool dunefd::ShSeg::GetCloseHits(
                detinfo::DetectorPropertiesData const& detProp,
		double r2d, 
		const std::vector< art::Ptr<recob::Hit> >& hits_in, 
		std::vector<size_t>& used,
		std::vector< art::Ptr<recob::Hit> >& hits_out)
{
	
	hits_out.clear();

	const double gapMargin = 5.0; // can be changed to f(id_tpc1, id_tpc2)
	size_t idx = 0;

	while ((idx < hits_in.size()) && Has(used, idx)) idx++;

	if (idx < hits_in.size())
	{		
		hits_out.push_back(hits_in[idx]);
		used.push_back(idx);

		double r2d2 = r2d*r2d;
		double gapMargin2 = sqrt(2 * gapMargin*gapMargin);
		gapMargin2 = (gapMargin2 + r2d)*(gapMargin2 + r2d);

		bool collect = true;
		while (collect)
		{
			collect = false;
			for (size_t i = 0; i < hits_in.size(); i++)
				if (!Has(used, i))
				{
					art::Ptr<recob::Hit> hi = hits_in[i];
                                        TVector2 hi_cm = pma::WireDriftToCm(detProp, hi->WireID().Wire, hi->PeakTime(), hi->WireID().Plane, hi->WireID().TPC, hi->WireID().Cryostat);

					bool accept = false;
					//for (auto const& ho : hits_out)
					for (size_t idx_o = 0; idx_o < hits_out.size(); idx_o++)					
					{
						art::Ptr<recob::Hit> ho = hits_out[idx_o];

						double d2 = pma::Dist2(
                                                                       hi_cm, pma::WireDriftToCm(detProp, ho->WireID().Wire, ho->PeakTime(), ho->WireID().Plane, ho->WireID().TPC, ho->WireID().Cryostat));
						
						if (hi->WireID().TPC == ho->WireID().TPC)
						{
							if (d2 < r2d2) { accept = true; break; }
						}
						else
						{
							if (d2 < gapMargin2) { accept = true; break; }
						}
					}
					if (accept)
					{
						collect = true;
						hits_out.push_back(hi);
						used.push_back(i);
					}
				}
		}
		return true;
	}
	else return false;
}

DEFINE_ART_MODULE(dunefd::ShSeg)
