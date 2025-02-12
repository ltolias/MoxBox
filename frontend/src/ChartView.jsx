import React from 'react'
import _ from 'underscore'

import {
    Chart as ChartJS,
    CategoryScale,
    LinearScale,
    PointElement,
    LineElement,
    Title,
    Tooltip,
    Legend,
    LineController
  } from 'chart.js';
  import zoomPlugin from 'chartjs-plugin-zoom';
  import { Chart } from 'react-chartjs-2';

const ChartView = (props) => {

    let chart_options = {
        maintainAspectRatio: false,
        responsive: true,
        plugins: {
            legend: { position: 'bottom' },
            title: {
                display: true,
                text: 'History',
            },
            zoom: {
                zoom: {
                wheel: { enabled: true },
                pinch: { enabled: true },
                mode: 'x',
                },
                pan: { enabled: true, mode: 'x', }
            }
        },
        scales: {
            x: {
                grid: { color: 'rgb(45, 42, 42)' }
            },
            y: {
                type: 'linear',
                display: false,
                position: 'left',
                grid: { color: 'rgb(45, 42, 42)', }
            }
        }
    };
    ChartJS.register(
        CategoryScale,
        LinearScale,
        PointElement,
        LineElement,
        LineController,
        Title,
        Tooltip,
        Legend,
        zoomPlugin,
    );
    const colors = ['rgb(55, 132, 162)', 'rgb(255, 153, 0)', 'rgb(0, 255, 60)']
    const data_names = _.keys(props.data)

    const chart_data = {
        labels: props.data[data_names[0]].map(row => row.time),
        datasets: _.map(data_names, (data_name, i) => {
            chart_options.scales[`y${i}`] = {
                type: 'linear',
                display: true,
                position: 'right',
                grid: { drawOnChartArea: false, },
            }
            return {
                type: 'line',
                label: data_name, 
                data: props.data[data_name].map(row => row.value), 
                borderColor: colors[i % colors.length],
                tension: 0.05,
                yAxisID: `y${i}`,
                borderWidth: 1,
                pointStyle: false
            }
        })
    }
    return <Chart className='chart' ref={props.chartRef} data={chart_data} options={chart_options}/>
}

export default ChartView
