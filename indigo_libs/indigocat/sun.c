// Created by Rumen Bogdanovski, 2022
//
// THIS SOFTWARE IS PROVIDED BY THE AUTHORS 'AS IS' AND ANY EXPRESS
// OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
// WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
// ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
// DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
// DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
// GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
// WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
// NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
// SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
//
//	Created by Rumen Bogdanovski, based on Liam Girdwood's code.

#include <math.h>
#include <stdio.h>
#include <solar_system.h>
#include <nutation.h>
#include <transform.h>

void sun_geometric_coords(double JD, heliocentric_coords_s * position) {
	earth_heliocentric_coords(JD, position);

	position->L += 180.0;
	position->L = range_degrees(position->L);
	position->B *= -1.0;
}

void sun_equatorial_coords(double JD, equatorial_coords_s * position) {
	heliocentric_coords_s sol;
	lonlat_coords_s LB;
	nutation_s nutation;
	double aberration;

	sun_geometric_coords(JD, &sol);

	/* add nutation */
	get_nutation(JD, &nutation);
	sol.L += nutation.longitude;

	/* aberration */
	aberration = (20.4898 / (360 * 60 * 60)) / sol.R;
	sol.L -= aberration;

	/* transform to equatorial */
	LB.lat = sol.B;
	LB.lon = sol.L;
	ecliptical_to_equatorial_coords(&LB, JD, position);
}
