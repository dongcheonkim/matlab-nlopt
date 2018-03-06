
#include "mexNLopt.h"
#include "nlopt_algorigthm_idstr.h"
#include "mexObjectiveFunction.h"

#include <mexObjectHandler.h>
#include <mex.h>

#include <nlopt.h>
#include <cctype>
#include <sstream>
#include <vector>
// #include <unordered_map>
#include <algorithm>
#include <memory>

class mexNLopt;

void mexFunction(int nlhs, mxArray *plhs[], int nrhs, const mxArray *prhs[])
{
  mexObjectHandler<mexNLopt>(nlhs, plhs, nrhs, prhs);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Implementation of mexNLopt class member functions

/**
   * \brief Copy assignment
   */
mexNLopt &mexNLopt::operator=(const mexNLopt &src)
{
  if (opt)
    nlopt_destroy(opt);
  opt = nlopt_copy(src.opt);
  return *this;
}

/**
   * \brief Copy assignment
   */
mexNLopt &mexNLopt::operator=(mexNLopt &&src) noexcept
{
  if (opt)
    nlopt_destroy(opt);
  opt = std::move(src.opt);
  return *this;
}

/**
   * \brief Performs static (object-independent) actions
   */
bool mexNLopt::static_handler(std::string command, int nlhs, mxArray *plhs[], int nrhs, const mxArray *prhs[])
{
  if (command == "getNLoptVersion")
    plhs[0] = mexNLopt::getNLoptVersion();
  else if (command == "getAlgorithms")
    plhs[0] = mexNLopt::getAlgorithms(prhs[0]); // name_desc = getAlgorithms(incl_desc)
  else
    return false;
  return true;
}

/**
   * \brief Performs object-dependent actions
   */
bool mexNLopt::action_handler(const mxArray *mxObj, const std::string &command, int nlhs, mxArray *plhs[], int nrhs, const mxArray *prhs[])
{
  // action command is looked up in the maps to get the pointer to the member function
  //    action_map - unqualified member functions
  //    const_action_map - const-qualified member functions
  // maps are static const variable, defined at the bottom of this file
  auto action_iterator = action_map.find(command);
  if (action_iterator != action_map.end())
    (this->*(action_iterator->second))(mxObj, nlhs, plhs, nrhs, prhs);
  else
  {
    auto const_action_iterator = const_action_map.find(command);
    if (const_action_iterator == const_action_map.end())
      return false;
    (this->*(const_action_iterator->second))(mxObj, nlhs, plhs, nrhs, prhs);
  }
  return true;
}

mxArray *mexNLopt::getNLoptVersion()
{
  int major, minor, bugfix;
  nlopt_version(&major, &minor, &bugfix);

  std::ostringstream buf;
  buf << major << '.' << minor << '.' << bugfix;

  return mxCreateString(buf.str().c_str());
}

/**
 * \brief Return algorithm name given algorithm enum
 */
std::string mexNLopt::get_algorithm_name_string(nlopt_algorithm a)
{
  if (a == NLOPT_NUM_ALGORITHMS)
    throw mexRuntimeError("invalidAlgorithm", "Invalid algorithm name.");
  std::string name = nlopt_algorithm_idstrs[(int)a] + 6;
  std::for_each(name.begin(), name.end(), [](auto &ch) { ch = tolower(ch); });
  return name;
}

/**
 * \brief Return algorithm name given algorithm enum
 */
mxArray *mexNLopt::get_algorithm_name(nlopt_algorithm a)
{
  return mxCreateString(get_algorithm_name_string(a).c_str());
}

/**
 * \brief Return algorithm description given algorithm enum
 */
mxArray *mexNLopt::get_algorithm_desc(nlopt_algorithm a)
{
  if (a == NLOPT_NUM_ALGORITHMS)
    throw mexRuntimeError("invalidAlgorithm", "Invalid algorithm name.");
  return mxCreateString(nlopt_algorithm_name(a));
}

/**
 * \brief Returns a cellstr array of all available algorithms
 */
mxArray *mexNLopt::getAlgorithms(const mxArray *incl_desc)
{
  int N = (int)NLOPT_NUM_ALGORITHMS;
  mxArray *rval = mxCreateCellMatrix(N, incl_desc ? 2 : 1);
  for_each_algorithm([&](const nlopt_algorithm a) -> bool {
    mxSetCell(rval, (mwIndex)a, mexNLopt::get_algorithm_name(a));
    if (mxGetLogicals(incl_desc)[0])
      mxSetCell(rval, N + (mwIndex)a, mexNLopt::get_algorithm_desc(a));
    return true;
  });
  return rval;
}

/**
 * \brief Returns algorithm given by mxString
 */
nlopt_algorithm mexNLopt::find_algorithm_by_name(const mxArray *mxStr)
{
  std::string name = mexGetString(mxStr);
  nlopt_algorithm algo = NLOPT_NUM_ALGORITHMS;
  for_each_algorithm([&](const nlopt_algorithm a) -> bool {
    bool rval;
    if (rval = (name == get_algorithm_name_string(a)))
      algo = a;
    return !rval;
  });

  // not found
  if (algo == NLOPT_NUM_ALGORITHMS)
    throw mexRuntimeError("invalidAlgorithm", "Invalid algorithm name.");

  return algo;
}

/**
 * \brief Initialize nlopt object
 */
void mexNLopt::init(const mxArray *mxAlgorithm, const mxArray *mxDim)
{
  nlopt_algorithm alg = find_algorithm_by_name(mxAlgorithm); // throw exception if not a valid algorithm name
  int dim = (int)mxGetScalar(mxDim);                         // assume prevalidated in MATLAB

  if (opt)
    nlopt_destroy(opt);

  opt = nlopt_create(alg, dim);
  if (opt == NULL)
    throw mexRuntimeError("failedNLoptCreation", "nlopt_create() failed to create a new nlopt object.");
}

void mexNLopt::getXTolAbs(MEX_ACTION_ARGUMENTS) const
{
  unsigned d = nlopt_get_dimension(opt);
  std::vector<double> tol(d);
  if (nlopt_get_xtol_abs(opt, tol.data()) < 0)
    throw mexRuntimeError("failedNLoptGet", "nlopt_get_xtol_abs() failed.");
  double tol0 = tol[0];
  if (d == 1 || std::all_of(tol.begin() + 1, tol.end(), [tol0](const double t) { return t == tol0; }))
    plhs[0] = mxCreateDoubleScalar(tol0);
  else
  {
    mxArray *rval = mxCreateDoubleMatrix(d, 1, mxREAL);
    std::copy(tol.begin(), tol.end(), mxGetPr(rval));
    plhs[0] = rval;
  }
}

void mexNLopt::getInitialStep(MEX_ACTION_ARGUMENTS) const
{
  unsigned d = nlopt_get_dimension(opt);
  std::vector<double> steps(d);
  std::vector<double> x0(d, 1.0);
  if (nlopt_get_initial_step(opt, x0.data(), steps.data()) < 0)
    throw mexRuntimeError("failedNLoptGet", "nlopt_get_initial_step() failed.");
  double dx0 = steps[0];
  if (d == 1 || std::all_of(steps.begin() + 1, steps.end(), [dx0](const double dx) { return dx == dx0; }))
    plhs[0] = mxCreateDoubleScalar(dx0);
  else
  {
    mxArray *rval = mxCreateDoubleMatrix(d, 1, mxREAL);
    std::copy(steps.begin(), steps.end(), mxGetPr(rval));
    plhs[0] = rval;
  }
}

mxArray *mexNLopt::getInitialStep(const mxArray *x0) const
{
  unsigned d = nlopt_get_dimension(opt);
  std::vector<double> steps(d);
  if (nlopt_get_initial_step(opt, mxGetPr(x0), steps.data()) < 0)
    throw mexRuntimeError("failedNLoptGet", "nlopt_get_initial_step() failed.");
  double dx0 = steps[0];
  if (d == 1 || std::all_of(steps.begin() + 1, steps.end(), [dx0](const double dx) { return dx == dx0; }))
    return mxCreateDoubleScalar(dx0);
  else
  {
    mxArray *rval = mxCreateDoubleMatrix(d, 1, mxREAL);
    std::copy(steps.begin(), steps.end(), mxGetPr(rval));
    return rval;
  }
}

// OPTIMIZATION ROUTINES
std::pair<nlopt_func,nlopt_precond> mexNLopt::config_obj_fun(nlopt_opt opt, mexObjectiveFunction &data)
{
  // objective function lambda
  auto f = [](unsigned n, const double *x, double *gradient, void *d_) -> double {
    mexObjectiveFunction &data = *(mexObjectiveFunction *)d_;
    double f = data.evalFun(n, x, gradient);
    if (data.stop)
      nlopt_force_stop(data.opt);
    return f;
  };

  // Hessian multiplier function lambda
  auto pre = [](unsigned n, const double *x, const double *v, double *vpre, void *f_data) {
    mexObjectiveFunction &data = *(mexObjectiveFunction *)f_data;
    data.evalHessMultFcn(n, x, v, vpre);
    if (data.stop)
      nlopt_force_stop(data.opt);
  };

  if (data.hessmult_args[0]) // if HessMultFcn given, set both simultaneously
  {
    nlopt_set_precond_min_objective(opt, f, pre, &data);
  }
  else // else just objective function
  {
    nlopt_set_min_objective(opt, f, &data);
  }

  return std::make_pair(f,pre);
}

void mexNLopt::fminunc(MEX_ACTION_ARGUMENTS)
{ // x = fminunc(fun,x0) (prevalidated inputs)
  // create a new evaluator object
  mexObjectiveFunction data(opt, (mxArray *)prhs[0], mxGetProperty(mxObj, 0, "HessMultFcn"), mxGetProperty(mxObj, 0, "OutputFun"));

  // set up objective function (and Hessian Multiplier function if given)
  auto funs = mexNLopt::config_obj_fun(opt,data);

  // create output x vector from prevalidated initial x
  plhs[0] = mxDuplicateArray(prhs[1]); 

  // go!
  mexNLopt::run_n_report(nlhs, plhs, opt, data);
}

void mexNLopt::fmincon(MEX_ACTION_ARGUMENTS) // nrhs=8, prhs=(fun,x0,con,mcon,coneq,mconeq,lb,ub)
{
  // work with a copy of the options so all the added constaints will not affect reruns
  nlopt_opt temp_opt = nlopt_copy(opt);
  std::unique_ptr<nlopt_opt_s, decltype(nlopt_destroy) *> onCleanup(temp_opt, nlopt_destroy); // auto-destroys temp_opt when done

  // create a new evaluator object
  mexObjectiveFunction data(temp_opt, (mxArray *)prhs[0], mxGetProperty(mxObj, 0, "HessMultFcn"), mxGetProperty(mxObj, 0, "OutputFun"));

  // set up objective function (and Hessian Multiplier function if given)
  auto funs = mexNLopt::config_obj_fun(temp_opt,data);

  // set local_options if specified
  set_local_optimizer(temp_opt, mxObj);

  // configure constraints
  double tol = mxGetScalar(mxGetProperty(mxObj,0,"ConstraintTolerance"));
  
  // keep nonlinear constraint lambdas in a vector
  std::vector<nlopt_func>   con_funs;
  std::vector<nlopt_mfunc> mcon_funs;
  if (!mxIsEmpty(prhs[2])) // con
  {
    for (int i = 0; i<mxGetNumberOfElements(prhs[2]))
    {
      
    }
  }

// typedef void (*nlopt_mfunc)(unsigned m, double *result,
// 			    unsigned n, const double *x,
// 			     double *gradient, /* NULL if not needed */
// 			     void *func_data);


  set_bounds(temp_opt, prhs[6], prhs[7]);

  // create output x vector from prevalidated initial x
  plhs[0] = mxDuplicateArray(prhs[1]); 

  // go!
  mexNLopt::run_n_report(nlhs, plhs, temp_opt, data);
}

void mexNLopt::set_bounds(nlopt_opt opt, const mxArray *mxLB, const mxArray *mxUB)
{
  if (!mxIsEmpty(mxLB) && nlopt_set_lower_bounds(opt, mxGetPr(mxLB))<0)
      throw mexRuntimeError("badLowerBound","Setting lower bound triggered an error.");

  if (!mxIsEmpty(mxUB) && nlopt_set_upper_bounds(opt, mxGetPr(mxUB))<0)
      throw mexRuntimeError("badUpperBound","Setting upper bound triggered an error.");
}

void mexNLopt::run_n_report(int nlhs, mxArray *plhs[], nlopt_opt opt, mexObjectiveFunction &data)
{
  // run init OutputFun if assigned
  data.evalOutputFun(true);

  // run the NLopt
  double *x = mxGetPr(plhs[0]); // get the vector
  double fval;
  nlopt_result res = nlopt_optimize(opt, x, &fval);
  switch (res) // take care of fatal failures
  {
  case NLOPT_FAILURE:
    throw mexRuntimeError("fminunc:failedGeneric", "nlopt_optimize() failed.");
  case NLOPT_INVALID_ARGS:
    throw mexRuntimeError("fminunc:invalidArgument", "Invalid optimization options.");
  case NLOPT_OUT_OF_MEMORY:
    throw mexRuntimeError("fminunc:outOfMemory", "Insufficient memory for the job.");
  case NLOPT_FORCED_STOP:
    if (data.lasterror) // something went wrong while calling Matlab
      throw mexRuntimeError(mexGetString(mxGetProperty(data.lasterror, 0, "identifier")), mexGetString(mxGetProperty(data.lasterror, 0, "message")));
  }

  // run init OutputFun if assigned
  data.evalOutputFun(false);

  // return additional arguments if asked
  if (nlhs > 1) // fval
  {
    plhs[1] = mxCreateDoubleScalar(fval);
    if (nlhs > 2) // exitflag
    {
      switch (res) // take care of fatal failures
      {
      case NLOPT_ROUNDOFF_LIMITED:
        plhs[2] = mxCreateDoubleScalar(-5.0);
        break;
      case NLOPT_FORCED_STOP:
        plhs[2] = mxCreateDoubleScalar(-1.0);
        break;
      case NLOPT_STOPVAL_REACHED:
        plhs[2] = mxCreateDoubleScalar(-3.0);
        break;
      case NLOPT_FTOL_REACHED:
        plhs[2] = mxCreateDoubleScalar(3.0);
        break;
      case NLOPT_XTOL_REACHED:
        plhs[2] = mxCreateDoubleScalar(2.0);
        break;
      case NLOPT_SUCCESS:
      case NLOPT_MAXEVAL_REACHED:
      case NLOPT_MAXTIME_REACHED:
      default:
        plhs[2] = mxCreateDoubleScalar(0.0);
        break;
      }
      if (nlhs > 3) // output.funCount
      {
        plhs[3] = mxCreateDoubleScalar(nlopt_get_numevals(opt));

        if (nlhs > 4) //grad
        {
          plhs[4] = data.evalGrad(plhs[0]);

          if (nlhs > 5) // hessian
          {
            plhs[5] = mxCreateDoubleMatrix(0, 0, mxREAL);
          }
        }
      }
    }
  }
}

void mexNLopt::set_local_optimizer(nlopt_opt opt, const mxArray *mxObj)
{
  mxArray *mxSubproblemAlgorithm = mxGetProperty(mxObj, 0, "SubproblemAlgorithm");
  if (mxIsEmpty(mxSubproblemAlgorithm)) // none set
    return;

  // if set, guaranteed to be another nlopt.options scalar object, get the mexNLopt object
  mexNLopt &obj = mexObjectHandle<mexNLopt>::getObject(mxGetProperty(mxSubproblemAlgorithm, 0, "backend"));

  // check to make sure the dimension matches
  if (nlopt_get_dimension(opt) != nlopt_get_dimension(obj.opt))
    throw mexRuntimeError("invalidSubproblem", "Subproblem dimension does not match that of the main problem.");

  // set the algorithm as sub
  if (nlopt_set_local_optimizer(opt, obj.opt) < 0)
    throw mexRuntimeError("unknownError", "nlopt_set_local_optimizer() failed.");
}

// NLOPT_EXTERN(nlopt_result) nlopt_set_default_initial_step(nlopt_opt opt, const double *x);

// algorithm outcome
// NLOPT_EXTERN(int) nlopt_get_numevals(const nlopt_opt opt);

// NLOPT_EXTERN(nlopt_result) nlopt_force_stop(nlopt_opt opt);
// NLOPT_EXTERN(nlopt_result) nlopt_set_force_stop(nlopt_opt opt, int val);
// NLOPT_EXTERN(int) nlopt_get_force_stop(const nlopt_opt opt);

// NLOPT_EXTERN(nlopt_result) nlopt_set_precond_min_objective(nlopt_opt opt, nlopt_func f, nlopt_precond pre, void *f_data);

/* constraints: */

// NLOPT_EXTERN(nlopt_result) nlopt_remove_inequality_constraints(nlopt_opt opt);
// NLOPT_EXTERN(nlopt_result) nlopt_add_inequality_constraint(nlopt_opt opt, nlopt_func fc, void *fc_data, double tol);
// NLOPT_EXTERN(nlopt_result) nlopt_add_precond_inequality_constraint(nlopt_opt opt, nlopt_func fc, nlopt_precond pre, void *fc_data, double tol);
// NLOPT_EXTERN(nlopt_result) nlopt_add_inequality_mconstraint(nlopt_opt opt, unsigned m,nlopt_mfunc fc,void *fc_data,const double *tol);

// NLOPT_EXTERN(nlopt_result) nlopt_remove_equality_constraints(nlopt_opt opt);
// NLOPT_EXTERN(nlopt_result) nlopt_add_equality_constraint(nlopt_opt opt, nlopt_func h, void *h_data, double tol);
// NLOPT_EXTERN(nlopt_result) nlopt_add_precond_equality_constraint(nlopt_opt opt, nlopt_func h, nlopt_precond pre, void *h_data, double tol);
// NLOPT_EXTERN(nlopt_result) nlopt_add_equality_mconstraint(nlopt_opt opt,unsigned m,nlopt_mfunc h,void *h_data,const double *tol);

const std::unordered_map<std::string, mexNLopt::action_fcn> mexNLopt::action_map =
    {
        {"fmincon", &mexNLopt::fmincon},
        {"fminunc", &mexNLopt::fminunc},
        {"setFunctionStopValue", &mexNLopt::setStopVal},
        {"setFunctionAbsoluteTolerance", &mexNLopt::setFTolAbs},
        {"setMaxFunctionEvaluations", &mexNLopt::setMaxEval},
        {"setFunctionRelativeTolerance", &mexNLopt::setFTolRel},
        {"setInitialStepSize", &mexNLopt::setInitialStep},
        {"setPopulation", &mexNLopt::setPopulation},
        {"setStepAbsoluteTolerance", &mexNLopt::setXTolAbs},
        {"setStepRelativeTolerance", &mexNLopt::setXTolRel},
        {"setMaxEvaluationDuration", &mexNLopt::setMaxTime},
        {"setVectorStorage", &mexNLopt::setVectorStorage}};

const std::unordered_map<std::string, mexNLopt::const_action_fcn> mexNLopt::const_action_map =
    {
        {"getAlgorithm", &mexNLopt::getAlgorithm},
        {"getDimension", &mexNLopt::getDimension},
        {"getFunctionAbsoluteTolerance", &mexNLopt::getFTolAbs},
        {"getFunctionRelativeTolerance", &mexNLopt::getFTolRel},
        {"getFunctionStopValue", &mexNLopt::getStopVal},
        {"getInitialStepSize", &mexNLopt::getInitialStep},
        {"getMaxEvaluationDuration", &mexNLopt::getMaxTime},
        {"getMaxFunctionEvaluations", &mexNLopt::getMaxEval},
        {"getPopulation", &mexNLopt::getPopulation},
        {"getStepAbsoluteTolerance", &mexNLopt::getXTolAbs},
        {"getStepRelativeTolerance", &mexNLopt::getXTolRel},
        {"getVectorStorage", &mexNLopt::getVectorStorage}};
