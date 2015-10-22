# -*- coding: utf-8 -*-
"""
Module containing classes used to model the mapping between 
stellar mass and subhalo_table. 
"""
from __future__ import (
    division, print_function, absolute_import, unicode_literals)

import numpy as np
from scipy.interpolate import UnivariateSpline
from astropy.extern import six
from abc import ABCMeta, abstractmethod, abstractproperty
from astropy import cosmology
from warnings import warn
from functools import partial

from .scatter_models import LogNormalScatterModel
from .smhm_model_template import PrimGalpropModel

from .. import model_defaults
from .. import model_helpers as model_helpers

from ...utils.array_utils import custom_len
from ...sim_manager import sim_defaults 


__all__ = ['Behroozi10SmHm']

class Behroozi10SmHm(PrimGalpropModel):
    """ Stellar-to-halo-mass relation based on 
    Behroozi et al. (2010), arXiv:1205.5807. 
    """

    def __init__(self, **kwargs):
        """
        Parameters 
        ----------
        prim_haloprop_key : string, optional  
            String giving the column name of the primary halo property governing stellar mass. 
            Default is set in the `~halotools.empirical_models.model_defaults` module. 

        scatter_model : object, optional  
            Class governing stochasticity of stellar mass. Default scatter is log-normal, 
            implemented by the `LogNormalScatterModel` class. 

        scatter_abcissa : array_like, optional  
            Array of values giving the abcissa at which
            the level of scatter will be specified by the input ordinates.
            Default behavior will result in constant scatter at a level set in the 
            `~halotools.empirical_models.model_defaults` module. 

        scatter_ordinates : array_like, optional  
            Array of values defining the level of scatter at the input abcissa.
            Default behavior will result in constant scatter at a level set in the 
            `~halotools.empirical_models.model_defaults` module. 
        """
        self.littleh = 0.7

        super(Behroozi10SmHm, self).__init__(
            galprop_key='stellar_mass', **kwargs)

        self.publications = ['arXiv:1001.0015']

    def retrieve_default_param_dict(self):
        """ Method returns a dictionary of all model parameters 
        set to the column 2 values in Table 2 of Behroozi et al. (2010). 

        Returns 
        -------
        d : dict 
            Dictionary containing parameter values. 
        """
        # All calculations are done internally using the same h=0.7 units 
        # as in Behroozi et al. (2010), so the parameter values here are 
        # the same as in Table 2, even though the mean_log_halo_mass and 
        # mean_stellar_mass methods use accept and return arguments in h=1 units. 

        d = {
        'smhm_m0_0': 10.72, 
        'smhm_m0_a': 0.59, 
        'smhm_m1_0': 12.35, 
        'smhm_m1_a': 0.3,
        'smhm_beta_0': 0.43,
        'smhm_beta_a': 0.18, 
        'smhm_delta_0': 0.56, 
        'smhm_delta_a': 0.18, 
        'smhm_gamma_0': 1.54,  
        'smhm_gamma_a': 2.52}

        return d

    def mean_log_halo_mass(self, log_stellar_mass, redshift=sim_defaults.default_redshift):
        """ Return the halo mass of a central galaxy as a function 
        of the stellar mass.  

        Parameters 
        ----------
        log_stellar_mass : array
            Array of base-10 logarithm of stellar masses in h=1 solar mass units. 

        redshift : float or array, optional 
            Redshift of the halo hosting the galaxy. If passing an array, 
            must be of the same length as the input ``log_stellar_mass``. 
            Default is set in `~halotools.sim_manager.sim_defaults`. 

        Returns 
        -------
        log_halo_mass : array_like 
            Array containing 10-base logarithm of halo mass in h=1 solar mass units. 
        """
        stellar_mass = (10.**log_stellar_mass)*(self.littleh**2)
        a = 1./(1. + redshift)

        logm0 = self.param_dict['smhm_m0_0'] + self.param_dict['smhm_m0_a']*(a - 1)
        m0 = 10.**logm0
        logm1 = self.param_dict['smhm_m1_0'] + self.param_dict['smhm_m1_a']*(a - 1)
        beta = self.param_dict['smhm_beta_0'] + self.param_dict['smhm_beta_a']*(a - 1)
        delta = self.param_dict['smhm_delta_0'] + self.param_dict['smhm_delta_a']*(a - 1)
        gamma = self.param_dict['smhm_gamma_0'] + self.param_dict['smhm_gamma_a']*(a - 1)

        stellar_mass_by_m0 = stellar_mass/m0
        term3_numerator = (stellar_mass_by_m0)**delta
        term3_denominator = 1 + (stellar_mass_by_m0)**(-gamma)

        log_halo_mass = logm1 + beta*np.log10(stellar_mass_by_m0) + (term3_numerator/term3_denominator) - 0.5

        return np.log10((10.**log_halo_mass)/self.littleh)

    def mean_stellar_mass(self, **kwargs):
        """ Return the stellar mass of a central galaxy as a function 
        of the input halo_table.  

        Parameters 
        ----------
        prim_haloprop : array, optional 
            Array of mass-like variable upon which occupation statistics are based. 
            If ``prim_haloprop`` is not passed, then ``halo_table`` keyword argument must be passed. 

        halo_table : object, optional 
            Data table storing halo catalog. 
            If ``halo_table`` is not passed, then ``prim_haloprop`` keyword argument must be passed. 

        redshift : float or array
            Redshift of the halo hosting the galaxy. If passing an array, 
            must be of the same length as the input ``stellar_mass``. 
            Default is set in `~halotools.sim_manager.sim_defaults`. 

        Returns 
        -------
        mstar : array_like 
            Array containing stellar masses living in the input halo_table, 
            in solar mass units assuming h = 1.
        """
        # Retrieve the array storing the mass-like variable
        if 'halo_table' in kwargs.keys():
            halo_mass = kwargs['halo_table'][self.prim_haloprop_key]
        elif 'prim_haloprop' in kwargs.keys():
            halo_mass = kwargs['prim_haloprop']
        else:
            raise KeyError("Must pass one of the following keyword arguments to mean_occupation:\n"
                "``halo_table`` or ``prim_haloprop``")

        if 'redshift' in kwargs:
            redshift = kwargs['redshift']
        else:
            redshift = sim_defaults.default_redshift

        log_stellar_mass_table = np.linspace(8.5, 12.5, 100)
        log_halo_mass_table = self.mean_log_halo_mass(log_stellar_mass_table, redshift=redshift)

        interpol_func = model_helpers.custom_spline(log_halo_mass_table, log_stellar_mass_table)

        log_stellar_mass = interpol_func(np.log10(halo_mass))

        stellar_mass = 10.**log_stellar_mass

        return stellar_mass



